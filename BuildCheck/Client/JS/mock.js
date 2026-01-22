// mock.js - fallback אם ה-API לא זמין

const MOCK_TYPES = [
  { type: "סדק", details: "זוהו קווי סדיקה אופייניים. מומלץ בדיקה בשטח." },
  { type: "רטיבות", details: "חשד לרטיבות/כתמי מים. מומלץ מד לחות." },
  { type: "התקלפות צבע", details: "התקלפות שכבת צבע/טיח באזור מקומי." },
  { type: "שבר", details: "אזור שבור/פגום. מומלץ תיעוד נוסף." },
  { type: "לא זוהה", details: "אין זיהוי חד. נסה תמונה חדה יותר." },
];

export function mockAnalyze() {
  // מחזיר תוצאה דמו אחרי דיליי קצר
  return new Promise((resolve) => {
    const pick = MOCK_TYPES[Math.floor(Math.random() * MOCK_TYPES.length)];
    setTimeout(() => resolve(pick), 650);
  });
}
