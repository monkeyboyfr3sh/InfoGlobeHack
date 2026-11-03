// Text encoding and frame compilation. Mirrors hello_world/main/src/globe_frame.c
// so the designer can show exact bytes and preview without the globe.

export const TEXT_MAX = 35;
const MISSING = new Set('%&+;@[\\]^_`{|}~');

const ACCENTS = new Map([
  ['à', 0x01], ['á', 0x02], ['â', 0x03], ['ä', 0x04], ['è', 0x05], ['é', 0x06], ['ê', 0x07], ['ë', 0x08],
  ['ì', 0x09], ['í', 0x0a], ['î', 0x0b], ['ï', 0x0c], ['ò', 0x0d], ['ó', 0x0e], ['ô', 0x0f], ['ö', 0x10],
  ['ù', 0x11], ['ú', 0x12], ['û', 0x13], ['ü', 0x14], ['Ç', 0x15], ['ñ', 0x16], ['É', 0x1a],
  ['✉', 0x80], ['📧', 0x80], ['📩', 0x80],
]);

// Suggested replacements for characters the globe cannot draw.
export const SUGGEST = {
  '%': 'pct', '+': 'plus', '&': 'and', '@': 'at', '_': '-', ';': ',', '[': '(', ']': ')',
  '{': '(', '}': ')', '|': '/', '\\': '/', '~': '-', '°': 'o', 'ç': 'Ç', 'Ñ': 'ñ', '’': "'", '“': '"', '”': '"',
};

/** UTF-8 string -> {codes, bad: [{ch, index}]}. Unsupported characters become spaces. */
export function encodeText(text) {
  const codes = [];
  const bad = [];
  let i = 0;
  for (const ch of text) {
    const cp = ch.codePointAt(0);
    if (cp >= 0x20 && cp < 0x7f && !MISSING.has(ch)) codes.push(cp);
    else if (ACCENTS.has(ch)) codes.push(ACCENTS.get(ch));
    else { codes.push(0x20); bad.push({ ch, index: i }); }
    i++;
  }
  return { codes, bad };
}

export const AFTER = ['spin', 'hold', 'flash', 'instant'];

function withText(cmd, codes) { return [cmd, 0x00, ...codes]; }

function stillFrame(codes, style) {
  const len = codes.length;
  // Flash = still text with every character blinking (blink byte len + 1);
  // command 02 only flashed part of longer text on the real globe.
  if (style.after === 'flash') return [...withText(0x05, codes), 0x00, len + 1, style.shift >= 0 ? Math.min(style.shift, 255) : len];
  if (style.after === 'instant') return [...withText(0x01, codes), 0x00];
  let blink = style.blink > 0 ? style.blink + 1 : 0;   // byte n blinks the first n-1 characters
  if (blink > len + 1) blink = len + 1;
  const shift = style.shift >= 0 ? Math.min(style.shift, 255) : len;
  return [...withText(0x05, codes), 0x00, blink, shift];
}

/**
 * style: {entrance: -1|0..37, after: 'spin'|'hold'|'flash'|'instant', blink: n, shift: -1|n}
 * Returns {first: bytes, then: bytes|null}.
 */
export function compileFrame(codes, style) {
  if (codes.length > TEXT_MAX) throw new Error('Text is longer than 35 characters');
  if (style.entrance >= 0) {
    return { first: [...withText(0x00, codes), style.entrance], then: style.after === 'spin' ? null : stillFrame(codes, style) };
  }
  if (style.after === 'spin') return { first: [...withText(0x04, codes), 0x00], then: null };
  return { first: stillFrame(codes, style), then: null };
}

export function hex(bytes) {
  return bytes.map((b) => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');
}
