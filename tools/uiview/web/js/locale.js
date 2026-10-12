// Resolve the UI language from the server's system locale, then the browser.
// Portuguese variants (including pt-PT and Brazilian locale aliases) use PT;
// everything else, including unavailable/unknown values, safely uses English.
"use strict";

function uiLanguageFromLocale(locale) {
  if (typeof locale !== "string" || !locale.trim()) return "eng";
  const normalized = locale.trim().replace(/_/g, "-").toLowerCase();
  return /(^|-)pt(-|$)/.test(normalized) ? "bra" : "eng";
}

async function detectUiLanguage() {
  try {
    const response = await fetch("/api/locale", { cache: "no-store" });
    if (response.ok) {
      const data = await response.json();
      if (typeof data.locale === "string" && data.locale.trim()) return uiLanguageFromLocale(data.locale);
    }
  } catch (_) { /* static viewer / server without locale endpoint */ }
  try {
    return uiLanguageFromLocale(navigator.language);
  } catch (_) {
    return "eng";
  }
}