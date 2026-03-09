import cv2
import numpy as np
import argparse
import os
from tqdm import tqdm

def create_chroma_hint(input_path, output_path):
    print(f"[Info] Generating rough alpha hint for {input_path}")
    
    cap = cv2.VideoCapture(input_path)
    if not cap.isOpened():
        print(f"Error opening {input_path}")
        return

    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))

    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    out = cv2.VideoWriter(output_path, fourcc, fps, (width, height), isColor=False)

    # Basic Green Screen HSV bounds (can be tuned per video)
    lower_green = np.array([35, 40, 40])
    upper_green = np.array([85, 255, 255])

    for _ in tqdm(range(total_frames), desc="Extracting rough matte"):
        ret, frame = cap.read()
        if not ret:
            break
            
        # Convert to HSV
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        
        # Create mask for green
        mask = cv2.inRange(hsv, lower_green, upper_green)
        
        # Invert mask (subject becomes white, green becomes black)
        alpha = cv2.bitwise_not(mask)
        
        # Morphological operations to clean up the rough hint
        kernel = np.ones((5,5), np.uint8)
        alpha = cv2.morphologyEx(alpha, cv2.MORPH_OPEN, kernel)
        alpha = cv2.morphologyEx(alpha, cv2.MORPH_CLOSE, kernel)
        
        # Blur the hint heavily so the Neural Network uses it just as a guide
        alpha = cv2.GaussianBlur(alpha, (21, 21), 0)

        out.write(alpha)

    cap.release()
    out.release()
    print(f"[Success] Saved alpha hint to {output_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=str, required=True)
    parser.add_argument("--output", type=str, required=True)
    args = parser.parse_args()
    
    create_chroma_hint(args.input, args.output)
