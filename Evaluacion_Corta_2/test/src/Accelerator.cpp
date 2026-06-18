#include "Accelerator.h"

#include <cstring>
#include <sstream>
#include <algorithm>

Accelerator::Accelerator(sc_core::sc_module_name name)
    : sc_module(name)
    , cfg_socket("cfg_socket")
    , mem_socket("mem_socket")
    , input_addr_(0)
    , output_addr_(0)
    , num_pixels_(0)
    , control_(0)
    , status_(STATUS_IDLE)
{
    cfg_socket.register_b_transport(this, &Accelerator::b_transport);

    SC_THREAD(process_thread);

    SC_REPORT_INFO("Accelerator", "Módulo Accelerator creado");
}

void Accelerator::b_transport(tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay)
{
    const tlm::tlm_command cmd = trans.get_command();
    const uint64_t addr = trans.get_address();
    uint8_t* data = trans.get_data_ptr();
    const unsigned int len = trans.get_data_length();

    if (data == nullptr) {
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }

    // For this simple register model, only 32-bit accesses are supported.
    if (len != 4) {
        std::ostringstream oss;
        oss << "Acceso inválido a registro: addr=0x"
            << std::hex << addr
            << " len=" << std::dec << len
            << " ; se esperan 4 bytes";
        SC_REPORT_WARNING("Accelerator", oss.str().c_str());

        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }

    uint32_t value = 0;

    if (cmd == tlm::TLM_WRITE_COMMAND) {
        std::memcpy(&value, data, sizeof(uint32_t));
        write_reg(static_cast<uint32_t>(addr), value);
    }
    else if (cmd == tlm::TLM_READ_COMMAND) {
        value = read_reg(static_cast<uint32_t>(addr));
        std::memcpy(data, &value, sizeof(uint32_t));
    }
    else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    delay += sc_core::sc_time(10, sc_core::SC_NS);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

uint32_t Accelerator::read_reg(uint32_t offset) const
{
    switch (offset) {
        case REG_INPUT_ADDR:
            return input_addr_;

        case REG_OUTPUT_ADDR:
            return output_addr_;

        case REG_NUM_PIXELS:
            return num_pixels_;

        case REG_CONTROL:
            return control_;

        case REG_STATUS:
            return status_;

        default:
            SC_REPORT_WARNING("Accelerator", "Lectura de registro desconocido");
            return 0;
    }
}

void Accelerator::write_reg(uint32_t offset, uint32_t value)
{
    switch (offset) {
        case REG_INPUT_ADDR:
            input_addr_ = value;
            break;

        case REG_OUTPUT_ADDR:
            output_addr_ = value;
            break;

        case REG_NUM_PIXELS:
            num_pixels_ = value;
            break;

        case REG_CONTROL:
            control_ = value;

            if ((value & CONTROL_START) != 0) {
                if (status_ == STATUS_BUSY) {
                    SC_REPORT_WARNING("Accelerator",
                                      "START recibido mientras el acelerador está ocupado");
                } else {
                    start_event_.notify(sc_core::SC_ZERO_TIME);
                }
            }
            break;

        case REG_STATUS:
            // Usually status is read-only. Allow clearing DONE by writing 0.
            if (value == STATUS_IDLE) {
                status_ = STATUS_IDLE;
                control_ = 0;
            }
            break;

        default:
            SC_REPORT_WARNING("Accelerator", "Escritura a registro desconocido");
            break;
    }
}

void Accelerator::process_thread()
{
    while (true) {
        wait(start_event_);

        status_ = STATUS_BUSY;

        std::ostringstream start_msg;
        start_msg << "Inicio procesamiento | input=0x" << std::hex << input_addr_
                  << " output=0x" << output_addr_
                  << " pixels=" << std::dec << num_pixels_
                  << " | t=" << sc_core::sc_time_stamp();
        SC_REPORT_INFO("Accelerator", start_msg.str().c_str());

        if (num_pixels_ == 0) {
            SC_REPORT_WARNING("Accelerator", "num_pixels_ es 0");
            status_ = STATUS_ERROR;
            continue;
        }

        try {
            constexpr uint32_t BLOCK_PIXELS = 1024;
            constexpr uint32_t RGB_BYTES_PER_PIXEL = 3;
            constexpr uint32_t GRAY_BYTES_PER_PIXEL = 1;

            std::vector<uint8_t> rgb_buf(BLOCK_PIXELS * RGB_BYTES_PER_PIXEL);
            std::vector<uint8_t> gray_buf(BLOCK_PIXELS * GRAY_BYTES_PER_PIXEL);

            uint32_t processed = 0;

            while (processed < num_pixels_) {
                uint32_t pixels_this_block =
                    std::min(BLOCK_PIXELS, num_pixels_ - processed);

                uint32_t rgb_len = pixels_this_block * RGB_BYTES_PER_PIXEL;
                uint32_t gray_len = pixels_this_block * GRAY_BYTES_PER_PIXEL;

                uint32_t rgb_addr =
                    input_addr_ + processed * RGB_BYTES_PER_PIXEL;

                uint32_t gray_addr =
                    output_addr_ + processed * GRAY_BYTES_PER_PIXEL;

                mem_read(rgb_addr, rgb_buf.data(), rgb_len);

                for (uint32_t i = 0; i < pixels_this_block; ++i) {
                    uint8_t r = rgb_buf[i * 3 + 0];
                    uint8_t g = rgb_buf[i * 3 + 1];
                    uint8_t b = rgb_buf[i * 3 + 2];

                    // Integer approximation of:
                    // gray = 0.299R + 0.587G + 0.114B
                    uint16_t gray =
                        static_cast<uint16_t>(
                            (77u * r + 150u * g + 29u * b) >> 8
                        );

                    gray_buf[i] = static_cast<uint8_t>(gray);
                }

                mem_write(gray_addr, gray_buf.data(), gray_len);

                processed += pixels_this_block;

                // Synthetic accelerator latency.
                wait(sc_core::sc_time(100, sc_core::SC_NS));
            }

            status_ = STATUS_DONE;
            control_ = 0;

            std::ostringstream done_msg;
            done_msg << "Procesamiento terminado | pixels=" << num_pixels_
                     << " | t=" << sc_core::sc_time_stamp();
            SC_REPORT_INFO("Accelerator", done_msg.str().c_str());
        }
        catch (...) {
            status_ = STATUS_ERROR;
            SC_REPORT_ERROR("Accelerator", "Error durante procesamiento");
        }
    }
}

void Accelerator::mem_read(uint32_t addr, uint8_t* data, uint32_t len)
{
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(data);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    trans.set_byte_enable_ptr(nullptr);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    mem_socket->b_transport(trans, delay);

    if (!trans.is_response_ok()) {
        SC_REPORT_ERROR("Accelerator", "mem_read falló");
    }

    wait(delay);
}

void Accelerator::mem_write(uint32_t addr, const uint8_t* data, uint32_t len)
{
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    // TLM expects non-const data pointer.
    uint8_t* non_const_data = const_cast<uint8_t*>(data);

    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(non_const_data);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    trans.set_byte_enable_ptr(nullptr);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    mem_socket->b_transport(trans, delay);

    if (!trans.is_response_ok()) {
        SC_REPORT_ERROR("Accelerator", "mem_write falló");
    }

    wait(delay);
}