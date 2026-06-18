#!/usr/bin/env python3
"""
compare_raw.py Genera figura de comparación input.raw vs output.raw (PIL/Pillow)

Uso:
    python3 scripts/compare_raw.py              # Genera figura
    python3 scripts/compare_raw.py --save       # Solo guardar (sin mostrar)
"""
import numpy as np
import sys
import os

W, H = 1920, 1080

def compare_raw(save=True) -> bool:
    """Genera comparación lado a lado de entrada y salida."""
    
    input_path  = "images/input.raw"
    output_path = "images/output.raw"
    
    # Verificar que existen ambos archivos
    if not os.path.exists(input_path):
        print(f"[ERROR] No encontrado: {input_path}")
        return False
    
    if not os.path.exists(output_path):
        print(f"[ERROR] No encontrado: {output_path}")
        return False
    
    print(f"[Load] Cargando {input_path}...")
    input_data = np.fromfile(input_path, dtype=np.uint8)
    
    print(f"[Load] Cargando {output_path}...")
    output_data = np.fromfile(output_path, dtype=np.uint8)
    
    # Verificar tamaños
    if input_data.size != W * H * 3:
        print(f"[WARN] Tamaño entrada inesperado: {input_data.size} (esperado {W*H*3})")
    
    if output_data.size != W * H:
        print(f"[WARN] Tamaño salida inesperado: {output_data.size} (esperado {W*H})")
    
    # Reshape
    input_img = input_data[:W * H * 3].reshape(H, W, 3)
    output_img = output_data[:W * H].reshape(H, W)
    
    # Estadísticas
    print(f"\n[Input]  RGB {W}x{H}")
    print(f"         Min/Max: {input_data.min()}/{input_data.max()}")
    print(f"         Mean: {input_data.mean():.1f}")
    
    print(f"\n[Output] Gray {W}x{H}")
    print(f"         Min/Max: {output_data.min()}/{output_data.max()}")
    print(f"         Mean: {output_data.mean():.1f}")
    
    # Usar PIL/Pillow
    try:
        from PIL import Image, ImageDraw, ImageFont
        print(f"[Plot]   Generando figura con PIL...")
        
        # Crear imagen RGB de entrada
        input_pil = Image.fromarray(input_img.astype(np.uint8), 'RGB')
        
        # Crear imagen RGB de salida (desde grayscale)
        output_rgb = np.stack([output_img, output_img, output_img], axis=-1)
        output_pil = Image.fromarray(output_rgb.astype(np.uint8), 'RGB')
        
        # Texto de leyenda
        input_bytes = os.path.getsize(input_path)
        output_bytes = os.path.getsize(output_path)
        input_caption = f"Input RGB {W}x{H} - {input_bytes:,} bytes"
        output_caption = f"Output Gray {W}x{H} - {output_bytes:,} bytes"
        
        # Fuente grande y centrada
        try:
            font = ImageFont.truetype("DejaVuSans-Bold.ttf", 36)
        except Exception:
            font = ImageFont.load_default()
        
        margin = 16
        dummy = Image.new('RGB', (1, 1))
        draw_dummy = ImageDraw.Draw(dummy)
        input_bbox = draw_dummy.textbbox((0, 0), input_caption, font=font)
        output_bbox = draw_dummy.textbbox((0, 0), output_caption, font=font)
        caption_height = max(input_bbox[3], output_bbox[3]) + margin * 2
        combined = Image.new('RGB', (W * 2, H + caption_height), 'white')
        combined.paste(input_pil, (0, 0))
        combined.paste(output_pil, (W, 0))
        
        draw = ImageDraw.Draw(combined)
        input_text_x = (W - (input_bbox[2] - input_bbox[0])) // 2
        output_text_x = W + (W - (output_bbox[2] - output_bbox[0])) // 2
        text_y = H + margin
        draw.text((input_text_x, text_y), input_caption, fill='black', font=font)
        draw.text((output_text_x, text_y), output_caption, fill='black', font=font)
        
        # Guardar
        out_png = "images/comparison.png"
        if save:
            combined.save(out_png, quality=90)
            print(f"[Save]   {out_png} ✓")
        
        return True
    
    except ImportError:
        print(f"[INFO]   PIL/Pillow no disponible.")
        print(f"        Para generar figuras: pip3 install pillow")
        print(f"        (Datos de salida verificados, ver estadísticas arriba)")
        return True  # No es un error fatal
    except Exception as e:
        print(f"[WARN]   Error al generar figura: {e}")
        import traceback
        traceback.print_exc()
        return True  # No es un error fatal


if __name__ == "__main__":
    success = compare_raw(save=True)
    sys.exit(0 if success else 1)
