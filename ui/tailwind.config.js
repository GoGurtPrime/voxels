/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{js,jsx}"],
  theme: {
    extend: {
      colors: {
        obsidian: {
          950: "#0a0b0f",
          900: "#11141b",
          800: "#191d28",
          700: "#232838",
          600: "#2f3648"
        },
        ember: {
          200: "#ffe7bd",
          300: "#ffd889",
          400: "#ffc35c",
          500: "#f2a93b",
          600: "#d98a1f",
          700: "#a8630f"
        },
        emerald: {
          400: "#5be3ac",
          500: "#34bd8a",
          600: "#22996e"
        },
        garnet: {
          400: "#ff7a6e",
          500: "#e6483f",
          600: "#b3312b"
        },
        parchment: {
          100: "#f9f0dc",
          200: "#ecd9b3",
          300: "#cdb98c"
        }
      },
      fontFamily: {
        display: ["Bungee", "ui-sans-serif", "system-ui", "sans-serif"],
        body: ["Nunito Sans", "ui-sans-serif", "system-ui", "sans-serif"]
      },
      boxShadow: {
        carved: "inset 0 1px 0 rgba(255,255,255,0.06), inset 0 -2px 0 rgba(0,0,0,0.45), 0 18px 40px -12px rgba(0,0,0,0.75)",
        "bevel-sm": "inset 0 1px 0 rgba(255,255,255,0.12), inset 0 -2px 0 rgba(0,0,0,0.4)"
      }
    }
  },
  plugins: []
};