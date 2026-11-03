import { useCallback, useState } from 'preact/hooks';

// Undo/redo for one value. Edits within `mergeMs` of each other (typing)
// become a single undo step.
export function useHistory(initial, mergeMs = 700) {
  const [h, setH] = useState({ past: [], present: initial, future: [], at: 0 });
  const set = useCallback((next, { replace = false } = {}) => setH((o) => {
    const value = typeof next === 'function' ? next(o.present) : next;
    if (replace) return { past: [], present: value, future: [], at: 0 };
    const now = Date.now();
    const merge = now - o.at < mergeMs && o.past.length > 0;
    return { past: merge ? o.past : [...o.past.slice(-99), o.present], present: value, future: [], at: now };
  }), [mergeMs]);
  const undo = useCallback(() => setH((o) => (o.past.length
    ? { past: o.past.slice(0, -1), present: o.past[o.past.length - 1], future: [o.present, ...o.future], at: 0 } : o)), []);
  const redo = useCallback(() => setH((o) => (o.future.length
    ? { past: [...o.past, o.present], present: o.future[0], future: o.future.slice(1), at: 0 } : o)), []);
  return { value: h.present, set, undo, redo, canUndo: h.past.length > 0, canRedo: h.future.length > 0 };
}
