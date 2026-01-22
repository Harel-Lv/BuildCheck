// app.js - הלוגיקה הראשית: קורא קובץ -> שולח ל-API -> מציג תוצאה
import { $, setStatus, setResult, showPreview } from "./ui.js";
import { mockAnalyze } from "./mock.js";

const API_URL = "http://127.0.0.1:8080/api/accident/analyze";
// אם אתה מריץ את ה-Client דרך אותו דומיין של ה-API, אפשר לעשות פשוט:
// const API_URL = "/api/accident/analyze";

function extractDamageType(apiJson) {
  // תומך בכמה צורות JSON בלי להישבר
  // צורה צפויה אצלך: { ok: true/false, results: [ { damage_types: [...] } ] }
  if (!apiJson || typeof apiJson !== "object") {
    return { damageType: "לא זוהה", details: "תגובה לא תקינה מהשרת." };
  }

  const results = Array.isArray(apiJson.results) ? apiJson.results : [];
  if (results.length === 0) {
    return { damageType: "לא זוהה", details: "אין results בתגובה." };
  }

  // ניקח את התמונה הראשונה
  const r0 = results[0] || {};

  const types = Array.isArray(r0.damage_types) ? r0.damage_types : [];
  if (types.length === 0) {
    return {
      damageType: r0.ok ? "לא זוהה" : "לא זוהה",
      details: r0.error || "לא התקבלו סוגי נזק מהמערכת.",
    };
  }

  // מציגים סוג ראשון + שאר כסיכום
  const damageType = String(types[0]);
  const extra = types.slice(1).map(String);
  const details =
    extra.length > 0 ? `נמצאו גם: ${extra.join(", ")}` : "זוהה סוג נזק אחד.";

  return { damageType, details };
}

async function callApi({ file, vehicleType, year }) {
  const fd = new FormData();
  // ה-API שלך מחפש "images" כ-file field
  fd.append("images", file, file.name);

  // לפי ה-API שלך יש שדות חובה vehicle_type + year.
  // ב-UI אמרנו "אופציונלי", אבל בפועל השרת שלך יחזיר 400 אם חסר.
  // לכן פה: אם לא הוזן, נשים ערכי ברירת מחדל כדי שלא ייפול.
  fd.append("vehicle_type", vehicleType?.trim() || "Building");
  fd.append("year", String(year || 2024));

  const res = await fetch(API_URL, {
    method: "POST",
    body: fd,
  });

  const text = await res.text();
  let json = null;
  try {
    json = JSON.parse(text);
  } catch {
    // אם השרת מחזיר משהו שהוא לא JSON
    throw new Error(`Server returned non-JSON (status ${res.status})`);
  }

  if (!res.ok) {
    // ה-API שלך מחזיר { ok:false, error:{message...} }
    const msg =
      (json && json.error && json.error.message) ||
      `HTTP ${res.status}`;
    throw new Error(msg);
  }

  return json;
}

function wireUi() {
  const fileInput = $("damageImage");
  const analyzeBtn = $("analyzeBtn");

  if (fileInput) {
    fileInput.addEventListener("change", () => {
      const f = fileInput.files && fileInput.files[0];
      showPreview(f || null);
      if (f) {
        setStatus("תמונה נבחרה. לחץ על 'ניתוח תמונה'.", "idle");
        setResult({ damageType: "—", details: "—" });
      } else {
        setStatus("ממתין לקובץ…", "idle");
        setResult({ damageType: "—", details: "—" });
      }
    });
  }

  if (analyzeBtn) {
    analyzeBtn.addEventListener("click", async () => {
      const f = fileInput && fileInput.files && fileInput.files[0];
      const vehicleType = $("vehicleType")?.value || "";
      const yearVal = parseInt($("year")?.value || "", 10);

      if (!f) {
        setStatus("בחר תמונה לפני ניתוח.", "error");
        setResult({ damageType: "—", details: "לא נבחר קובץ." });
        return;
      }

      setStatus("מנתח…", "loading");
      setResult({ damageType: "—", details: "שולח לשרת…" });

      try {
        const apiJson = await callApi({
          file: f,
          vehicleType,
          year: Number.isFinite(yearVal) ? yearVal : 2024,
        });

        const { damageType, details } = extractDamageType(apiJson);
        setStatus("הניתוח הסתיים.", "ok");
        setResult({ damageType, details });

      } catch (err) {
        // fallback ל-mock כדי שהדמו לא יישבר
        console.warn("API failed, using mock:", err);

        setStatus("השרת לא זמין/שגיאה. מציג תוצאה לדמו.", "error");
        const mock = await mockAnalyze();
        setResult({ damageType: mock.type, details: mock.details });
      }
    });
  }
}

document.addEventListener("DOMContentLoaded", wireUi);
