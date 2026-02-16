// ui.js - רק UI helpers (אין פה רשת)

export function $(id) {
  return document.getElementById(id);
}

let currentPreviewUrl = null;

export function setText(id, text) {
  const el = $(id);
  if (el) el.textContent = text;
}

export function setStatus(text, kind = "idle") {
  // kind: idle | loading | ok | error
  setText("statusText", text);

  // אפשר בעתיד להוסיף צבעים דרך class, כרגע רק טקסט
  const el = $("statusText");
  if (!el) return;

  el.dataset.kind = kind;
}

export function showPreview(file) {
  const box = $("previewBox");
  const img = $("previewImg");
  if (!box || !img) return;

  if (currentPreviewUrl) {
    URL.revokeObjectURL(currentPreviewUrl);
    currentPreviewUrl = null;
  }

  if (!file) {
    box.style.display = "none";
    img.src = "";
    box.setAttribute("aria-hidden", "true");
    return;
  }

  const url = URL.createObjectURL(file);
  currentPreviewUrl = url;
  img.src = url;
  box.style.display = "block";
  box.setAttribute("aria-hidden", "false");

  // שחרור URL כשמחליפים תמונה
  img.onload = () => {
    if (currentPreviewUrl === url) {
      URL.revokeObjectURL(url);
      currentPreviewUrl = null;
    }
  };
}

export function setResult({ damageType = "—", details = "—" }) {
  setText("damageType", damageType);
  setText("details", details);
}
