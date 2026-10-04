import { Buffer } from "node:buffer";

export function escapeHtml(value: string): string {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

export function serializeJsonForScriptTag(
  value: unknown,
  replacer?: (key: string, value: unknown) => unknown,
): string {
  // Keep the wire text ASCII so one Unicode label does not turn every large
  // scene/HTML copy into a UTF-16 allocation. JSON parsing restores the exact
  // text, including surrogate pairs; template-breaking characters are safe.
  const text = JSON.stringify(value, replacer)
    .replace(/[<>&\u007f-\uffff]/g, (character) => {
      switch (character) {
        case "<": return "\\u003C";
        case ">": return "\\u003E";
        case "&": return "\\u0026";
        default: return "\\u" + character.charCodeAt(0).toString(16).padStart(4, "0");
      }
    });
  // V8 can retain a two-byte backing store even after the last Unicode
  // character was escaped. The resulting text is ASCII, so this conversion
  // gives it an actual one-byte backing store before assembling the HTML.
  return Buffer.from(text, "ascii").toString("latin1");
}

// Scene properties are plain protocol records and arrays. Serialize one array
// item at a time so we never allocate the entire scene as a temporary UTF-16
// JSON string before producing its safe one-byte wire representation.
export function serializeSceneForScriptTag(
  scene: Record<string, unknown>,
  replacer?: (key: string, value: unknown) => unknown,
): string {
  const properties: string[] = [];
  for (const [key, original] of Object.entries(scene)) {
    const value = replacer ? replacer(key, original) : original;
    if (value === undefined) continue;
    const encoded = Array.isArray(value)
      ? "[" + value.map(item => serializeJsonForScriptTag(item ?? null, replacer)).join(",") + "]"
      : serializeJsonForScriptTag(value, replacer);
    properties.push(serializeJsonForScriptTag(key) + ":" + encoded);
  }
  return "{" + properties.join(",") + "}";
}
