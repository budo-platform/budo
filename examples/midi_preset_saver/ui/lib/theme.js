/** Theme tokens. Colors are Budo colors; sizes are density-independent
 *  pixels (dp), scaled by the display density when widgets draw. */

/** @typedef {import('./color.js').Color} Color */
/** @typedef {{
 *   background: Color, surface: Color, raised: Color, ink: Color, muted: Color,
 *   border: Color, accent: Color, accentInk: Color, highlight: Color,
 *   danger: Color, focus: Color, shadow: Color,
 *   elevation: { y: number, blur: number }[], accentGradient: Color[] | null,
 *   font: number, small: number, row: number, radius: number,
 *   gap: number, padding: number }} UITheme
 *
 *  `elevation` holds the drop shadows of levels 1 (raised), 2 (menus), and
 *  3 (dialogs, sheets), in dp, drawn in `shadow`. `accentGradient`, when set
 *  (top to bottom colors), replaces flat `accent` fills: primary buttons,
 *  progress bars, and switched-on toggles. */

/** @type {UITheme} */
const light = {
    background: '#F3F5F2', surface: '#FFFFFF', raised: '#E8EEEA',
    ink: '#20312F', muted: '#61716D', border: '#C9D5CF',
    accent: '#006F67', accentInk: '#FFFFFF', highlight: '#F3AC59',
    danger: '#C0392B', focus: '#006F67', shadow: '#1A2B2A40',
    elevation: [{ y: 2, blur: 4 }, { y: 6, blur: 12 }, { y: 10, blur: 20 }], accentGradient: null,
    font: 20, small: 16, row: 48, radius: 5, gap: 8, padding: 16,
};

/** @type {UITheme} */
const dark = {
    ...light,
    background: '#121718', surface: '#1B2223', raised: '#263031',
    ink: '#E4ECEA', muted: '#8E9E9B', border: '#34413F',
    accent: '#3FB8AC', accentInk: '#062320', highlight: '#F3AC59',
    danger: '#E5675B', focus: '#3FB8AC', shadow: '#00000080',
};

export const themes = { light, dark };
