import { $, setStatus, setResult, showPreview } from "./ui.js";

function resolveApiBaseFromParams() {
  try {
    const url = new URL(window.location.href);
    const q = String(url.searchParams.get("api_base") || "").trim();
    if (q) return q.replace(/\/$/, "");
  } catch {
  }
  return "";
}

function resolveAnalyzeApiUrl() {
  const fromParams = resolveApiBaseFromParams();
  if (fromParams) return `${fromParams}/api/property/analyze`;
  if (typeof window !== "undefined" && typeof window.BUILDCHECK_API_URL === "string" && window.BUILDCHECK_API_URL.trim()) {
    return window.BUILDCHECK_API_URL.trim();
  }
  if (window.location.protocol === "file:") {
    try {
      const saved = String(localStorage.getItem("buildcheck_api_base") || "").trim();
      if (saved) return `${saved.replace(/\/$/, "")}/api/property/analyze`;
    } catch {
    }
    return "http://127.0.0.1:8080/api/property/analyze";
  }
  return "/api/property/analyze";
}

function resolveApiBase() {
  const fromParams = resolveApiBaseFromParams();
  if (fromParams) return fromParams;
  if (typeof window !== "undefined" && typeof window.BUILDCHECK_API_BASE === "string" && window.BUILDCHECK_API_BASE.trim()) {
    return window.BUILDCHECK_API_BASE.trim().replace(/\/$/, "");
  }
  if (window.location.protocol === "file:") {
    try {
      const saved = String(localStorage.getItem("buildcheck_api_base") || "").trim();
      if (saved) return saved.replace(/\/$/, "");
    } catch {
    }
    return "http://127.0.0.1:8080";
  }
  return "";
}

const ANALYZE_API_URL = resolveAnalyzeApiUrl();
const API_BASE = resolveApiBase();
const CONTACT_POST_URL = API_BASE ? `${API_BASE}/api/contact` : "/api/contact";
const REQUEST_TIMEOUT_MS = 20000;
const MAX_IMAGE_BYTES = 10 * 1024 * 1024;
const ALLOWED_IMAGE_EXT = new Set(["jpg", "jpeg", "png", "webp"]);

let analyzeInFlight = false;
let contactInFlight = false;

function extractDamageType(apiJson) {
  if (!apiJson || typeof apiJson !== "object") {
    return { damageType: "לא זוהה", inferenceMode: "" };
  }

  const results = Array.isArray(apiJson.results) ? apiJson.results : [];
  if (results.length === 0) {
    return { damageType: "לא זוהה", inferenceMode: "" };
  }

  const allTypes = [];
  const seen = new Set();
  let inferenceMode = "";

  for (const item of results) {
    if (!inferenceMode && item && typeof item.inference_mode === "string") {
      inferenceMode = item.inference_mode;
    }
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
    return { damageType: "לא זוהה", inferenceMode };
  }

  const firstType = allTypes[0] === "suspected_damage" ? "חשד לנזק" : allTypes[0];
  return { damageType: firstType, inferenceMode };
}

function validateImageFile(file) {
  if (!file) return "לא נבחר קובץ.";
  const name = String(file.name || "");
  const dot = name.lastIndexOf(".");
  const ext = dot >= 0 ? name.slice(dot + 1).toLowerCase() : "";
  if (!ALLOWED_IMAGE_EXT.has(ext)) {
    return "סוג קובץ לא נתמך. העלה JPG/PNG/WebP.";
  }
  if (file.size > MAX_IMAGE_BYTES) {
    return "הקובץ גדול מדי (מקסימום 10MB).";
  }
  return "";
}

async function callAnalyzeApi(file) {
  const fd = new FormData();
  fd.append("images", file, file.name);

  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), REQUEST_TIMEOUT_MS);
  let res;
  try {
    res = await fetch(ANALYZE_API_URL, {
      method: "POST",
      body: fd,
      signal: ctrl.signal,
    });
  } catch (err) {
    if (err && typeof err === "object" && err.name === "AbortError") {
      throw new Error("תם הזמן לניתוח. נסה שוב בעוד רגע.");
    }
    throw err;
  } finally {
    clearTimeout(timer);
  }

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
        setResult({ damageType: "—" });
      } else {
        setStatus("ממתין לקובץ…", "idle");
        setResult({ damageType: "—" });
      }
    });
  }

  if (analyzeBtn) {
    analyzeBtn.addEventListener("click", async () => {
      if (analyzeInFlight) return;
      const file = fileInput && fileInput.files && fileInput.files[0];

      if (!file) {
        setStatus("בחר תמונה לפני ניתוח.", "error");
        setResult({ damageType: "—" });
        return;
      }

      const fileErr = validateImageFile(file);
      if (fileErr) {
        setStatus("הניתוח נכשל.", "error");
        setResult({ damageType: "לא זוהה" });
        return;
      }

      analyzeInFlight = true;
      analyzeBtn.disabled = true;
      setStatus("מנתח…", "loading");
      setResult({ damageType: "—" });

      try {
        const apiJson = await callAnalyzeApi(file);
        const { damageType, inferenceMode } = extractDamageType(apiJson);
        const fallback = inferenceMode === "heuristic_fallback";
        if (fallback) {
          setStatus("הניתוח הושלם במצב גיבוי (ללא מודל מלא).", "error");
        } else {
          setStatus(apiJson && apiJson.ok ? "הניתוח הסתיים." : "הניתוח הושלם ללא זיהוי נזק.", apiJson && apiJson.ok ? "ok" : "error");
        }
        setResult({ damageType });
      } catch (err) {
        setStatus("הניתוח נכשל.", "error");
        setResult({ damageType: "לא זוהה" });
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
      const ctrl = new AbortController();
      const timer = setTimeout(() => ctrl.abort(), REQUEST_TIMEOUT_MS);
      let res;
      try {
        res = await fetch(CONTACT_POST_URL, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ name, phone, message }),
          signal: ctrl.signal,
        });
      } catch (errFetch) {
        if (errFetch && typeof errFetch === "object" && errFetch.name === "AbortError") {
          throw new Error("תם הזמן לשליחת הפנייה. נסה שוב.");
        }
        throw errFetch;
      } finally {
        clearTimeout(timer);
      }
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
