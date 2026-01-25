from ultralytics import YOLO

model = YOLO("models/mbdd2025/best_mbdd_yolo.pt")


results = model.predict(
    source="test.jpg",   # או תיקייה
    imgsz=640,
    conf=0.25,
    save=True
)

print("Inference done")
