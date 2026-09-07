import React, { useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import "./styles.css";

 const Route = {
   MainMenu: 0,
   SaveSelection: 1,
   WorldCreation: 2,
   Loading: 3,
   Join: 4,
   Error: 5,
   Pause: 6,
   Settings: 7,
   ControlsCard: 8
 };
 
 let nextRequestId = 1;
 
 function sendAction(kind, fields = {}) {
  window.voxelsAction?.(JSON.stringify({ kind, requestId: nextRequestId++, ...fields }));
 }
 
 function ActionButton({ children, kind, fields, className = "", ...props }) {
   return <button className={`action-button ${className}`} type="button" onClick={() => sendAction(kind, fields)} {...props}>{children}</button>;
 }
 
 function Shell({ title, children, blocking = false }) {
   const heading = useRef(null);
   useEffect(() => heading.current?.focus(), [title]);
   return <main className="player-ui" aria-busy={blocking}>
     <section className="menu-panel" aria-labelledby="route-title">
       <p className="kicker">Voxels Engine</p>
       <h1 id="route-title" tabIndex="-1" ref={heading}>{title}</h1>
       {children}
     </section>
   </main>;
 }
 
 function MainMenu() {
   return <Shell title="A world is waiting."><nav aria-label="Main menu" className="action-stack">
     <ActionButton kind="play">Play</ActionButton>
    <ActionButton kind="join">Join Game</ActionButton>
     <ActionButton kind="settings">Settings</ActionButton>
     <ActionButton className="quiet" kind="quit">Quit</ActionButton>
   </nav></Shell>;
 }
 
 function WorldSelect({ model }) {
   const [selected, setSelected] = useState("");
   const [confirmation, setConfirmation] = useState("");
  const worlds = model.items.filter((item) => item.startsWith("save:")).map((item) => { const [slot, name] = item.slice(5).split("|"); return { slot, name: name || slot }; });
  const target = selected || worlds[0]?.slot || "";
  const selectedWorld = worlds.find((world) => world.slot === target)?.name || target;
   return <Shell title="Select World"><p className="supporting">Choose a saved world or create a new one.</p>
    <fieldset className="world-list"><legend>Saved worlds</legend>{worlds.length ? worlds.map((world) => <label className="world-option" key={world.slot}><input type="radio" name="world" value={world.slot} checked={target === world.slot} onChange={() => setSelected(world.slot)} />{world.name}</label>) : <p>No worlds yet.</p>}</fieldset>
     <div className="action-stack"><ActionButton kind="create-world">New World</ActionButton><ActionButton kind="load-world" fields={{ primary: target }} disabled={!target}>Play Selected</ActionButton></div>
     {target ? <div className="danger-zone"><label htmlFor="delete-confirmation">Type <strong>{selectedWorld || target}</strong> to delete it.</label><input id="delete-confirmation" value={confirmation} onChange={(event) => setConfirmation(event.target.value)} autoComplete="off" /><ActionButton className="danger" kind="confirm-delete" fields={{ primary: target, secondary: confirmation }} disabled={confirmation !== (selectedWorld || target)}>Delete World</ActionButton></div> : null}
     <ActionButton className="quiet" kind="back">Back</ActionButton>
   </Shell>;
 }
 
 function WorldCreation() {
   const [name, setName] = useState("New World");
   const [seed, setSeed] = useState("");
   const [options, setOptions] = useState({ sandbox: false, peaceful: false, permadeath: false, sunny: false, public: false, distance: 8 });
   const update = (key, value) => setOptions({ ...options, [key]: value });
  return <Shell title="Create World"><form onSubmit={(event) => { event.preventDefault(); sendAction("create-world", { primary: name, secondary: JSON.stringify({ seed, ...options }) }); }}>
     <label>World name<input value={name} maxLength="48" onChange={(event) => setName(event.target.value)} required /></label><label>Seed <span className="optional">optional</span><input value={seed} maxLength="20" onChange={(event) => setSeed(event.target.value)} /></label>
    <fieldset className="options"><legend>World options</legend>{[["sandbox", "Creative mode"], ["peaceful", "Peaceful"], ["permadeath", "Permadeath"], ["sunny", "Always day"], ["public", "Public LAN world"]].map(([key, label]) => <label className="toggle" key={key}><input type="checkbox" checked={options[key]} onChange={(event) => update(key, event.target.checked)} />{label}</label>)}<label>Render distance<input type="number" min="2" max="16" value={options.distance} onChange={(event) => update("distance", Number.parseInt(event.target.value || "8", 10))} /></label></fieldset>
     <div className="action-stack"><button className="action-button" type="submit">Create World</button><ActionButton className="quiet" kind="back">Back</ActionButton></div>
   </form></Shell>;
 }
 
 function JoinGame() {
   const [host, setHost] = useState("127.0.0.1");
   const [port, setPort] = useState("27015");
   return <Shell title="Join Game"><form onSubmit={(event) => { event.preventDefault(); sendAction("join", { primary: host, secondary: port }); }}><p className="supporting">Enter the host address and UDP port.</p><label>Host address<input value={host} onChange={(event) => setHost(event.target.value)} required /></label><label>UDP port<input type="number" min="1" max="65535" value={port} onChange={(event) => setPort(event.target.value)} required /></label><div className="action-stack"><button className="action-button" type="submit">Connect</button><ActionButton className="quiet" kind="back">Back</ActionButton></div></form></Shell>;
 }
 
 function Loading({ model }) { return <Shell title={model.title || "Loading"} blocking><p className="supporting">{model.message || "Preparing your world..."}</p><progress value={model.progress} max="1">{Math.round(model.progress * 100)}%</progress><p>{Math.round(model.progress * 100)}%</p></Shell>; }
 function ErrorRoute({ model }) { return <Shell title={model.title || "Something went wrong"} blocking><p className="error-copy" role="alert">{model.message}</p><ActionButton kind="acknowledge-error">Return to Main Menu</ActionButton></Shell>; }
 function Controls() { return <Shell title="Controls"><dl className="controls"><dt>Move</dt><dd>W A S D</dd><dt>Look</dt><dd>Mouse</dd><dt>Jump</dt><dd>Space</dd><dt>Break / Place</dt><dd>Left / Right Click</dd><dt>Pause</dt><dd>Escape</dd></dl><ActionButton kind="dismiss-controls">Got it</ActionButton></Shell>; }
function Settings() {
  const [settings, setSettings] = useState({ fov: 80, renderDistance: 8, simulationDistance: 6, master: 1, music: 1, effects: 1, sensitivity: 1, invertY: false, particles: true });
  const update = (key, value) => setSettings({ ...settings, [key]: value });
  return <Shell title="Settings"><form onSubmit={(event) => { event.preventDefault(); sendAction("apply-settings", { secondary: JSON.stringify(settings) }); }}><label>Field of view<input type="range" min="60" max="110" value={settings.fov} onChange={(event) => update("fov", Number.parseFloat(event.target.value))} /></label><label>Render distance<input type="range" min="2" max="16" value={settings.renderDistance} onChange={(event) => update("renderDistance", Number.parseInt(event.target.value, 10))} /></label><label>Simulation distance<input type="range" min="2" max="12" value={settings.simulationDistance} onChange={(event) => update("simulationDistance", Number.parseInt(event.target.value, 10))} /></label><label>Master volume<input type="range" min="0" max="1" step="0.01" value={settings.master} onChange={(event) => update("master", Number.parseFloat(event.target.value))} /></label><label>Music volume<input type="range" min="0" max="1" step="0.01" value={settings.music} onChange={(event) => update("music", Number.parseFloat(event.target.value))} /></label><label>Effects volume<input type="range" min="0" max="1" step="0.01" value={settings.effects} onChange={(event) => update("effects", Number.parseFloat(event.target.value))} /></label><label>Mouse sensitivity<input type="range" min="0.1" max="4" step="0.1" value={settings.sensitivity} onChange={(event) => update("sensitivity", Number.parseFloat(event.target.value))} /></label><label className="toggle"><input type="checkbox" checked={settings.invertY} onChange={(event) => update("invertY", event.target.checked)} />Invert Y</label><label className="toggle"><input type="checkbox" checked={settings.particles} onChange={(event) => update("particles", event.target.checked)} />Particles</label><div className="action-stack"><button className="action-button" type="submit">Apply</button><ActionButton className="quiet" kind="back">Back</ActionButton></div></form></Shell>;
}
 
 function App() {
   const [model, setModel] = useState({ route: Route.MainMenu, revision: 0, progress: 0, items: [] });
  useEffect(() => {
    window.__voxelsReceiveModel = setModel;
    if (window.__voxelsLastModel) setModel(window.__voxelsLastModel);
    return () => delete window.__voxelsReceiveModel;
  }, []);
   switch (model.route) {
    case Route.Pause: return null;
     case Route.SaveSelection: return <WorldSelect model={model} />;
     case Route.WorldCreation: return <WorldCreation />;
     case Route.Loading: return <Loading model={model} />;
     case Route.Join: return <JoinGame />;
     case Route.Error: return <ErrorRoute model={model} />;
     case Route.Settings: return <Settings />;
     case Route.ControlsCard: return <Controls />;
     default: return <MainMenu />;
   }
 }
 
 createRoot(document.getElementById("root")).render(<App />);