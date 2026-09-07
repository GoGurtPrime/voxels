import React, { useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import "./styles.css";

function RuntimeProof() {
  const [model, setModel] = useState(null);

  useEffect(() => {
    window.__voxelsReceiveModel = (nextModel) => setModel(nextModel);
    return () => {
      delete window.__voxelsReceiveModel;
    };
  }, []);

  return (
    <main className="pointer-events-none min-h-screen bg-transparent p-8 text-white">
      <section className="w-fit border border-cyan-200/70 bg-slate-950/75 px-4 py-3 shadow-[0_0_24px_rgba(34,211,238,0.35)]">
        <p className="text-xs font-semibold uppercase tracking-[0.2em] text-cyan-200">Local Web UI</p>
        <h1 className="mt-1 text-xl font-bold">{model?.title ?? "Transparent runtime proof"}</h1>
        {model?.message ? <p className="mt-1 text-sm text-cyan-100/90">{model.message}</p> : null}
        {model ? <p className="mt-1 text-[10px] text-cyan-300/70">model revision {model.revision}</p> : null}
      </section>
    </main>
  );
}

createRoot(document.getElementById("root")).render(<RuntimeProof />);