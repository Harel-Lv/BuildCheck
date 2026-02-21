function $(id) {
  return document.getElementById(id);
}

function resolveApiBaseFromParams() {
  try {
    const url = new URL(window.location.href);
    const q = String(url.searchParams.get("api_base") || "").trim();
    if (q) return q.replace(/\/$/, "");
  } catch {
  }
  return "";
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

const API_BASE = resolveApiBase();
const ADMIN_LIST_URL = API_BASE ? `${API_BASE}/api/admin/contact/submissions` : "/api/admin/contact/submissions";
const ADMIN_LOGIN_URL = API_BASE ? `${API_BASE}/api/admin/login` : "/api/admin/login";
const ADMIN_LOGOUT_URL = API_BASE ? `${API_BASE}/api/admin/logout` : "/api/admin/logout";
const REQUEST_TIMEOUT_MS = 20000;

async function fetchJsonWithTimeout(url, options = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), REQUEST_TIMEOUT_MS);
  try {
    const res = await fetch(url, { ...options, signal: ctrl.signal });
    const data = await res.json().catch(() => ({}));
    return { res, data };
  } catch (err) {
    if (err && typeof err === "object" && err.name === "AbortError") {
      throw new Error("תם הזמן לבקשה. נסה שוב.");
    }
    throw err;
  } finally {
    clearTimeout(timer);
  }
}

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

async function login(username, password) {
  const { res, data } = await fetchJsonWithTimeout(ADMIN_LOGIN_URL, {
    method: "POST",
    credentials: "include",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password }),
  });
  if (!res.ok || !data || data.ok !== true) {
    const msg = data && data.error && data.error.message ? data.error.message : `HTTP ${res.status}`;
    throw new Error(msg);
  }
}

async function loadSubmissions() {
  const { res, data } = await fetchJsonWithTimeout(ADMIN_LIST_URL, {
    method: "GET",
    credentials: "include",
  });
  if (!res.ok || !data || data.ok !== true) {
    const msg = data && data.error && data.error.message ? data.error.message : `HTTP ${res.status}`;
    throw new Error(msg);
  }
  renderAdminList(data.items);
}

async function logout() {
  const { res, data } = await fetchJsonWithTimeout(ADMIN_LOGOUT_URL, {
    method: "POST",
    credentials: "include",
  });
  if (!res.ok || (data && data.ok === false)) {
    const msg = data && data.error && data.error.message ? data.error.message : `HTTP ${res.status}`;
    throw new Error(msg);
  }
}

function wireAdminPage() {
  const form = $("adminForm");
  const userInput = $("adminUsername");
  const passInput = $("adminPassword");
  const loginBtn = $("adminLoginBtn");
  const refreshBtn = $("adminRefreshBtn");
  const logoutBtn = $("adminLogoutBtn");
  if (!form || !userInput || !passInput || !loginBtn || !refreshBtn || !logoutBtn) return;

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const username = String(userInput.value || "").trim();
    const password = String(passInput.value || "").trim();
    if (!username || !password) {
      setAdminStatus("נדרש שם משתמש וסיסמה.", "error");
      return;
    }

    loginBtn.disabled = true;
    setAdminStatus("מתחבר…", "loading");
    try {
      await login(username, password);
      await loadSubmissions();
      setAdminStatus("התחברת בהצלחה.", "ok");
      passInput.value = "";
    } catch (e) {
      renderAdminList([]);
      const msg = e instanceof Error ? e.message : "שגיאת התחברות";
      setAdminStatus(msg, "error");
    } finally {
      loginBtn.disabled = false;
    }
  });

  refreshBtn.addEventListener("click", async () => {
    refreshBtn.disabled = true;
    setAdminStatus("טוען פניות…", "loading");
    try {
      await loadSubmissions();
      setAdminStatus("הפניות נטענו בהצלחה.", "ok");
    } catch (e) {
      const msg = e instanceof Error ? e.message : "שגיאה בטעינת פניות";
      setAdminStatus(msg, "error");
    } finally {
      refreshBtn.disabled = false;
    }
  });

  logoutBtn.addEventListener("click", async () => {
    logoutBtn.disabled = true;
    try {
      await logout();
      renderAdminList([]);
      setAdminStatus("התנתקת.", "idle");
    } catch (e) {
      const msg = e instanceof Error ? e.message : "שגיאת ניתוק";
      setAdminStatus(msg, "error");
    } finally {
      logoutBtn.disabled = false;
    }
  });
}

document.addEventListener("DOMContentLoaded", wireAdminPage);
