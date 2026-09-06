import React from "react";
import { createRoot } from "react-dom/client";
import "./styles.css";

function RuntimeProof() {
  return (
    <main className="pointer-events-none min-h-screen bg-transparent p-8 text-white">
      <section className="w-fit border border-cyan-200/70 bg-slate-950/75 px-4 py-3 shadow-[0_0_24px_rgba(34,211,238,0.35)]">
        <p className="text-xs font-semibold uppercase tracking-[0.2em] text-cyan-200">Local Web UI</p>
        <h1 className="mt-1 text-xl font-bold">Transparent runtime proof</h1>
      </section>
    </main>
  );
}

createRoot(document.getElementById("root")).render(<RuntimeProof />);