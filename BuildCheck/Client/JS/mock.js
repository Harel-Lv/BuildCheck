// mock.js - fallback אם ה-API לא זמין

const MOCK_TYPES = [
  { type: "סדק" },
  { type: "רטיבות" },
  { type: "התקלפות צבע" },
  { type: "שבר" },
  { type: "לא זוהה" },
];

export function mockAnalyze() {
  // מחזיר תוצאה דמו אחרי דיליי קצר
  return new Promise((resolve) => {
    const pick = MOCK_TYPES[Math.floor(Math.random() * MOCK_TYPES.length)];
    setTimeout(() => resolve(pick), 650);
  });
}
