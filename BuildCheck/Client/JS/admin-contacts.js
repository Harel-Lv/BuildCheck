function $(id) {
  return document.getElementById(id);
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

const API_BASE = resolveApiBase();
const ADMIN_LIST_URL = API_BASE ? `${API_BASE}/api/admin/contact/submissions` : "/api/admin/contact/submissions";

function setAdminStatus(text, kind = "idle") {
  const el = $("adminStatus");
  if (!el) return;
  el.textContent = text;
  el.dataset.kind = kind;
}

function formatRegisteredAt(value) {
  const d = new Date(value);
  if (Number.isNaN(d.getTime())) return String(value || "");
  return d.toLocaleString("he-IL");
}

function renderAdminList(items) {
  const list = $("adminContactList");
  if (!list) return;
  list.innerHTML = "";

  if (!Array.isArray(items) || items.length === 0) {
    const li = document.createElement("li");
    li.className = "contact-log__item";
    li.textContent = "אין פניות שמורות.";
    list.appendChild(li);
    return;
  }

  for (const item of items) {
    const li = document.createElement("li");
    li.className = "contact-log__item";

    const who = document.createElement("div");
    who.className = "contact-log__who";
    who.textContent = `${item.name || ""} | ${item.phone || ""}`;

    const when = document.createElement("div");
    when.className = "contact-log__when";
    when.textContent = `נרשם: ${formatRegisteredAt(item.registered_at)}`;

    const msg = document.createElement("div");
    msg.className = "contact-log__msg";
    msg.textContent = item.message || "";

    li.appendChild(who);
    li.appendChild(when);
    li.appendChild(msg);
    list.appendChild(li);
  }
}

function wireAdminPage() {
  const form = $("adminForm");
  const tokenInput = $("adminToken");
  const loadBtn = $("adminLoadBtn");
  if (!form || !tokenInput || !loadBtn) return;

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const token = String(tokenInput.value || "").trim();
    if (!token) {
      setAdminStatus("נדרש טוקן אדמין.", "error");
      return;
    }

    loadBtn.disabled = true;
    setAdminStatus("טוען פניות…", "loading");

    try {
      const res = await fetch(ADMIN_LIST_URL, {
        method: "GET",
        headers: {
          "X-Admin-Token": token,
        },
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data || data.ok !== true) {
        const msg = data && data.error && data.error.message ? data.error.message : `HTTP ${res.status}`;
        throw new Error(msg);
      }
      renderAdminList(data.items);
      setAdminStatus("הפניות נטענו בהצלחה.", "ok");
    } catch (e) {
      renderAdminList([]);
      const msg = e instanceof Error ? e.message : "שגיאה בטעינת פניות";
      setAdminStatus(msg, "error");
    } finally {
      loadBtn.disabled = false;
    }
  });
}

document.addEventListener("DOMContentLoaded", wireAdminPage);
