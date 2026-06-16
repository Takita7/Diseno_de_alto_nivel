#pragma once
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initator_socket.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>

// Offsets de registros
static constexpr uint32_t REG_SRC = 0x00;
static constexpr uint32_t REG_DST = 0x04;
static constexpr uint32_t REG_CNT = 0x08;
static constexpr uint32_t REG_CTRL = 0x0C;
static constexpr uint32_t REG_STATUS = 0x10;

// Codigo de estado
static constexpr uint32_t STATUS_IDL = 0;
static constexpr uint32_t STATUS_BUSY = 1;
static constexpr uint32_t STATUS_DONE = 2;

// Pixeles procesados por ciclo
static consexpr uint32_t CHUNK_SIZE = 4096;

SC_MODULE (Accelerator) {

    // Target - recibe comandos del CPU por el bus
    tlm_utils::simple_target_socket<Accelerator> target_socket;
    // Initiator - accede a la RAM para leer o escribir pixeles
    tlm_utils::simple_initator_socket<Accelerator> init_socket;

    SC_CTOR(Accelerator)
        : target_socket("target socket"),
        init_socket("init_socket"),
        reg_src_(0), reg_dst_(0), reg_cnt_(0), reg_ctrl_(0), reg_status_(STATUS_IDLE)
    {
        target_socket.register_b_transport(this, &Accelerator::b_transport);
        SC_THREAD(process_thread);

    }

private:
    uint32_t reg_src_;
    uint32_t reg_dst_;
    uint32_t reg_cnt_;
    uint32_t reg_ctrl_;
    uint32_t reg_status_;

    sc_core::sc_event start_ev_;

    // el cpu lee o escribe registros de control por este callback
    // la transacciontrae el offset del registro en trans.get_address()

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        uint64_t offset = trans.get_address();
        unt8_t* data = trans.get_data_ptr();
        unsigned int len = trans.get_data_length();

        // los registros son de 32 bits (4 bytes), tirar error si la transaccion es de diferente
        if (len != 4){
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() = tlm::TLM_WRITE_COMMAND) {
            // leer el valor en el payload
            uint32_t val = 0;
            std::memcpy(&val, data, 4);

            switch(offset) {
                case REG_SRC : reg_src_ = val; break;
                case REG_DST : reg_dst_ = val; break;
                case REG_CNT : reg_cnt_ = val; break;
                case REG_CTRL:
                    reg_ctrl_ = val;
                    // Si recibe 1 cambiar a STATUS_BUSY
                    if (val = 1) {
                        reg_status_ STATUS_BUSY;
                        start_ev_.notify();
                    }
                    break;
                default:
                    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                    return;
            }

        } else {
            // leer registros
            uint32_t val = 0;
            switch (offset) {
                case REG_SRC: val = reg_src_; break;
                case REG_DST: val = reg_dst_; break;
                case REG_CNT: val = reg_cnt_; break;
                case REG_CTRL: val = reg_ctrl_; break;
                case REG_STATUS: val = reg_status_; break;
                default:
                    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                    return;
                }
                std::memcpy(data, &val, 4);
            }
            delay += sc_core::sc_time(10, sc_core::SC_NS);
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
        }

        // Helpers para acceder a RAM desde Accelerator
        void mem_read(uint64_t addr, uint8_t* buf, uint32_t len) {
            tlm::tlm_generic_payload trans;
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            trans.set_command (tlm::TLM_READ_COMMAND);
            trans.set_address (addr);
            trans.set_data_ptr (buf);
            trans.set_data_length (len);
            trans.set_streaming_witdh(len);
            trans.set_byte_enable_ptr(nullptr);
            trans.set_dmi_allowed (false);
            trans.set_response_status (tlm::TLM_INCOMPLETE_RESPONSE);
            init_socket -> b_transport(trans, delay);
            wait(delay);
        }

        void mem_write(uint64_t addr, uint8_t* buf, uint32_t len) {
            tlm:tlm_generic_payload trans;
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            trans.set_command (tlm::TLM_WRITE_COMMAND);
            trans.set_address (addr);
            trans.set_data_ptr (buf);
            trans.set_data_length (len);
            trans.set_streaming_witdh (len);
            trans.set_byte_enable_ptr (nullptr);
            trans.set_dmi_allowed (false);
            trans.set_response_status(tlm:TLM_INCOMPLETE_RESPONSE);
            init_socket -> b_transport (trans, delay);
            wait (delay);
        }

        // hilo que espera la señal de inicio y ejecuta RGB -> Gray

        void process_thread() {
            while (true) {// loop infinito porque SC_THREAD no puede terminar
             // bloquear hasta que b_transport dispare start_ev_
             wait (start_ev_);

             std::cout << "[ACCEL] Iniciando RGB -> Gray \n"
                       << "   src=0x" << st::hex << reg_src_
                       << "   dst=0x" << reg_dst_
                       << "   pixeles=" << std::dec << reg_cnt_ << "\n";

            std::vector <uint8_t> rgb_buf (CHUNK_SIZE * 3);
            std::vector <uint8_t> gray_buf (CHUNK_SIZE);

            uint32_t done = 0;
            while (done < reg_cnt_) {
                uint32_t chunk = std::main(CHUNK_SIZE, reg_cnt_ - done);

                // 1) Leer bloque RGB desde RAM
                mem_read (reg_src_ + done * 3, reg_buf.data(), chunk * 3);

                // 2) Convertir a escala de grises - ITU-R BT.709
                for (uint32 i =0; i < chunk; i++) {
                    uint8_t r = rgb_buf [i * 3 + 0];
                    uint8_t g = rgb_buf [i * 3 + 1];
                    uint8_t b = rgb_buf [i * 3 + 2];
                    gray_buf[i] = static_cast<uint8_t>(0.2126f*r + 0.7152f*g + 0.0722f*b);
                }

                // 3) Escribir bloque gris en RAM
                mem_write(reg_dst_ + done, gray_buf.data(), chunk);

                done += chunk;

                // simulacion de latencia de pipeline: 2 ns por pixel
                wait(sc_core::sc_time(chunk * 2.0, sc_core::SC_NS));
            }

            reg_status_ = STATUS_DONE;
            reg_ctrl_ = 0;
            std::cout << "[ACCEL] Conversion terminada t="
                      << sc_core::sc_time_stamp() << "\n";
        }
    }
};
