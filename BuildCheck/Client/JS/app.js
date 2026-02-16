import { $, setStatus, setResult, showPreview } from "./ui.js";

function resolveAnalyzeApiUrl() {
  if (typeof window !== "undefined" && typeof window.BUILDCHECK_API_URL === "string" && window.BUILDCHECK_API_URL.trim()) {
    return window.BUILDCHECK_API_URL.trim();
  }
  if (window.location.protocol === "file:") {
    return "http://127.0.0.1:8080/api/property/analyze";
  }
  return "/api/property/analyze";
}

function resolveApiBase() {
  if (typeof window !== "undefined" && typeof window.BUILDCHECK_API_BASE === "string" && window.BUILDCHECK_API_BASE.trim()) {
    return window.BUILDCHECK_API_BASE.trim().replace(/\/$/, "");
  }
  if (window.location.protocol === "file:") {
    return "http://127.0.0.1:8080";
  }
  return "";
}

const ANALYZE_API_URL = resolveAnalyzeApiUrl();
const API_BASE = resolveApiBase();
const CONTACT_POST_URL = API_BASE ? `${API_BASE}/api/contact` : "/api/contact";

let analyzeInFlight = false;
let contactInFlight = false;

function extractDamageType(apiJson) {
  if (!apiJson || typeof apiJson !== "object") {
    return { damageType: "לא זוהה", details: "תגובה לא תקינה מהשרת." };
  }

  const results = Array.isArray(apiJson.results) ? apiJson.results : [];
  if (results.length === 0) {
    return { damageType: "לא זוהה", details: "אין results בתגובה." };
  }

  const allTypes = [];
  const seen = new Set();
  let firstError = "";
  let okCount = 0;

  for (const item of results) {
    if (item && item.ok) okCount += 1;
    if (!firstError && item && typeof item.error === "string") firstError = item.error;
    const itemTypes = Array.isArray(item && item.damage_types) ? item.damage_types : [];
    for (const t of itemTypes) {
      const key = String(t);
      if (!seen.has(key)) {
        seen.add(key);
        allTypes.push(key);
      }
    }
  }

  if (allTypes.length === 0) {
    return {
      damageType: "לא זוהה",
      details: firstError || "לא התקבלו סוגי נזק מהמערכת.",
    };
  }

  const damageType = allTypes[0];
  const extra = allTypes.slice(1);
  const details = `${okCount}/${results.length} תמונות זוהו. ${extra.length > 0 ? `נמצאו גם: ${extra.join(", ")}` : ""}`.trim();

  return { damageType, details };
}

async function callAnalyzeApi(file) {
  const fd = new FormData();
  fd.append("images", file, file.name);

  const res = await fetch(ANALYZE_API_URL, {
    method: "POST",
    body: fd,
  });

  const text = await res.text();
  let json = null;
  try {
    json = JSON.parse(text);
  } catch {
    throw new Error(`Server returned non-JSON (status ${res.status})`);
  }

  if (!res.ok) {
    if (res.status === 422 && json && Array.isArray(json.results)) {
      return json;
    }
    const msg = (json && json.error && json.error.message) || `HTTP ${res.status}`;
    throw new Error(msg);
  }

  return json;
}

function wireAnalyzeUi() {
  const fileInput = $("damageImage");
  const analyzeBtn = $("analyzeBtn");

  if (fileInput) {
    fileInput.addEventListener("change", () => {
      const file = fileInput.files && fileInput.files[0];
      showPreview(file || null);
      if (file) {
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
      if (analyzeInFlight) return;
      const file = fileInput && fileInput.files && fileInput.files[0];

      if (!file) {
        setStatus("בחר תמונה לפני ניתוח.", "error");
        setResult({ damageType: "—", details: "לא נבחר קובץ." });
        return;
      }

      analyzeInFlight = true;
      analyzeBtn.disabled = true;
      setStatus("מנתח…", "loading");
      setResult({ damageType: "—", details: "שולח תמונה לשרת…" });

      try {
        const apiJson = await callAnalyzeApi(file);
        const { damageType, details } = extractDamageType(apiJson);
        setStatus(apiJson && apiJson.ok ? "הניתוח הסתיים." : "הניתוח הושלם ללא זיהוי נזק.", apiJson && apiJson.ok ? "ok" : "error");
        setResult({ damageType, details });
      } catch (err) {
        const msg = err instanceof Error ? err.message : "שגיאת שרת לא ידועה";
        setStatus("הניתוח נכשל.", "error");
        setResult({ damageType: "לא זוהה", details: msg });
      } finally {
        analyzeInFlight = false;
        analyzeBtn.disabled = false;
      }
    });
  }
}

function setContactStatus(text, kind = "idle") {
  const el = $("contactStatus");
  if (!el) return;
  el.textContent = text;
  el.dataset.kind = kind;
}

function validateContactInput(name, phone, message) {
  if (name.length < 2 || name.length > 80) return "שם חייב להיות בין 2 ל-80 תווים.";
  if (!/^\+?[0-9()\-\s]{7,20}$/.test(phone)) return "פורמט טלפון לא תקין.";
  if (message.length < 5 || message.length > 2000) return "הודעה חייבת להיות בין 5 ל-2000 תווים.";
  return "";
}

function wireContactForm() {
  const form = $("contactForm");
  const nameInput = $("contactName");
  const phoneInput = $("contactPhone");
  const messageInput = $("contactMessage");
  const submitBtn = $("contactSubmitBtn");

  if (!form || !nameInput || !phoneInput || !messageInput || !submitBtn) return;

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    if (contactInFlight) return;

    const name = String(nameInput.value || "").trim();
    const phone = String(phoneInput.value || "").trim();
    const message = String(messageInput.value || "").trim();

    const err = validateContactInput(name, phone, message);
    if (err) {
      setContactStatus(err, "error");
      return;
    }

    contactInFlight = true;
    submitBtn.disabled = true;
    setContactStatus("שומר פרטים…", "loading");

    try {
      const res = await fetch(CONTACT_POST_URL, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, phone, message }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data || data.ok !== true) {
        const msg = data && data.error && data.error.message ? data.error.message : `HTTP ${res.status}`;
        throw new Error(msg);
      }

      form.reset();
      setContactStatus("הפרטים נשמרו בהצלחה.", "ok");
    } catch (e) {
      const msg = e instanceof Error ? e.message : "שגיאה בשמירת פרטים";
      setContactStatus(msg, "error");
    } finally {
      contactInFlight = false;
      submitBtn.disabled = false;
    }
  });
}

document.addEventListener("DOMContentLoaded", () => {
  wireAnalyzeUi();
  wireContactForm();
});
