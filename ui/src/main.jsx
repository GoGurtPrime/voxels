import React, { useEffect, useMemo, useRef, useState } from "react";
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

function ActionButton({ children, kind, fields, className = "", style, ...props }) {
  return (
    <button className={`action-button ${className}`} type="button" style={style} onClick={() => sendAction(kind, fields)} {...props}>
      {children}
    </button>
  );
}

/** Atmospheric layer standing in for the eventual scenic in-game background. */
function SceneBackdrop() {
  const embers = useMemo(() => Array.from({ length: 18 }, (_, index) => ({
    id: index,
    left: Math.round(Math.random() * 100),
    delay: Math.round(Math.random() * 9000),
    duration: Math.round(9000 + Math.random() * 7000),
    drift: Math.round((Math.random() - 0.5) * 80)
  })), []);
  return (
    <>
      <div className="scene-backdrop" aria-hidden="true" />
      <div className="ember-field" aria-hidden="true">
        {embers.map((ember) => (
          <span
            key={ember.id}
            className="ember"
            style={{
              left: `${ember.left}%`,
              animationDelay: `${ember.delay}ms`,
              animationDuration: `${ember.duration}ms`,
              "--drift": `${ember.drift}px`
            }}
          />
        ))}
      </div>
      <div className="scene-vignette" aria-hidden="true" />
    </>
  );
}

/** Shared frame for every route except the main menu's own hero layout. */
function RouteShell({ title, kicker = "Voxels Engine", subtitle, children, footer, blocking = false, wide = false }) {
  const heading = useRef(null);
  useEffect(() => heading.current?.focus(), [title]);
  return (
    <main className="player-ui" aria-busy={blocking} style={{ alignItems: "center", justifyContent: "center", padding: 24 }}>
      <section className="carved-panel route-shell anim-rise" style={wide ? { width: "min(760px, calc(100vw - 48px))" } : undefined} aria-labelledby="route-title">
        <div className="route-shell-header">
          <p className="kicker">{kicker}</p>
          <h1 id="route-title" className="route-title" tabIndex="-1" ref={heading}>{title}</h1>
          {subtitle ? <p className="supporting">{subtitle}</p> : null}
        </div>
        <div className="route-shell-body scroll-region">{children}</div>
        {footer ? <div className="route-shell-footer">{footer}</div> : null}
      </section>
    </main>
  );
}

function MainMenu() {
  const items = [
    { kind: "play", label: "Play", className: "" },
    { kind: "join", label: "Join Game", className: "quiet" },
    { kind: "settings", label: "Settings", className: "quiet" },
    { kind: "quit", label: "Quit", className: "ghost" }
  ];
  return (
    <main className="player-ui">
      <div className="hero-shell">
        <div className="hero-title-block anim-rise">
          <p className="kicker">Voxels Engine</p>
          <h1 className="hero-title anim-glow">Voxels</h1>
          <p className="hero-subtitle">A block-based world is waiting.</p>
        </div>
        <div className="hero-nav-row">
          <nav aria-label="Main menu" className="hero-nav">
            {items.map((item, index) => (
              <ActionButton
                key={item.kind}
                kind={item.kind}
                className={`${item.className} anim-rise`}
                style={{ animationDelay: `${120 + index * 70}ms` }}
              >
                {item.label}
              </ActionButton>
            ))}
          </nav>
          <p className="version-tag anim-rise" style={{ animationDelay: "460ms" }}>VoxelsEngine</p>
        </div>
      </div>
    </main>
  );
}

function WorldSelect({ model }) {
  const [selected, setSelected] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const worlds = model.items.filter((item) => item.startsWith("save:")).map((item) => {
    const [slot, name] = item.slice(5).split("|");
    return { slot, name: name || slot };
  });
  const target = selected || worlds[0]?.slot || "";
  const selectedWorld = worlds.find((world) => world.slot === target)?.name || target;
  return (
    <RouteShell
      title="Select World"
      subtitle="Choose a saved world or start a new one."
      footer={
        <div className="action-stack">
          <ActionButton kind="create-world">New World</ActionButton>
          <ActionButton kind="load-world" fields={{ primary: target }} disabled={!target}>Play Selected</ActionButton>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <fieldset style={{ border: "none", padding: 0, margin: 0 }}>
        <legend className="section-title">Saved worlds</legend>
        {worlds.length ? worlds.map((world) => (
          <label className="world-option" key={world.slot}>
            <input type="radio" name="world" value={world.slot} checked={target === world.slot} onChange={() => setSelected(world.slot)} />
            <span className="world-name">{world.name}</span>
          </label>
        )) : <p className="supporting">No worlds yet — create your first one below.</p>}
      </fieldset>
      {target ? (
        <div className="danger-zone">
          <p className="field-label" style={{ marginBottom: 8 }}>Type <strong>{selectedWorld || target}</strong> to delete it</p>
          <input id="delete-confirmation" value={confirmation} onChange={(event) => setConfirmation(event.target.value)} autoComplete="off" />
          <div style={{ marginTop: 10 }}>
            <ActionButton className="danger" kind="confirm-delete" fields={{ primary: target, secondary: confirmation }} disabled={confirmation !== (selectedWorld || target)}>Delete World</ActionButton>
          </div>
        </div>
      ) : null}
    </RouteShell>
  );
}

function Chip({ checked, onChange, children }) {
  return (
    <label className={`chip-toggle ${checked ? "on" : ""}`}>
      <input type="checkbox" checked={checked} onChange={(event) => onChange(event.target.checked)} />
      {children}
    </label>
  );
}

function WorldCreation() {
  const [name, setName] = useState("New World");
  const [seed, setSeed] = useState("");
  const [options, setOptions] = useState({ sandbox: false, peaceful: false, permadeath: false, sunny: false, public: false, distance: 8 });
  const update = (key, value) => setOptions({ ...options, [key]: value });
  const toggles = [
    ["sandbox", "Creative mode"],
    ["peaceful", "Peaceful"],
    ["permadeath", "Permadeath"],
    ["sunny", "Always day"],
    ["public", "Public LAN world"]
  ];
  return (
    <RouteShell
      title="Create World"
      subtitle="Name your world and choose how it plays."
      wide
      footer={
        <div className="action-stack">
          <button className="action-button" type="submit" form="world-creation-form">Create World</button>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <form id="world-creation-form" onSubmit={(event) => { event.preventDefault(); sendAction("create-world", { primary: name, secondary: JSON.stringify({ seed, ...options }) }); }}>
        <p className="section-title">Basics</p>
        <div className="field-grid">
          <label className="field-row">
            <span className="field-label">World name</span>
            <input value={name} maxLength="48" onChange={(event) => setName(event.target.value)} required />
          </label>
          <label className="field-row">
            <span className="field-label">Seed <span className="optional">optional</span></span>
            <input value={seed} maxLength="20" onChange={(event) => setSeed(event.target.value)} />
          </label>
        </div>

        <p className="section-title">Rules</p>
        <div className="toggle-grid">
          {toggles.map(([key, label]) => (
            <Chip key={key} checked={options[key]} onChange={(value) => update(key, value)}>{label}</Chip>
          ))}
        </div>

        <p className="section-title">Performance</p>
        <div className="slider-row">
          <span className="field-label">Render distance</span>
          <span className="slider-value">{options.distance} chunks</span>
        </div>
        <input type="range" min="2" max="16" value={options.distance} onChange={(event) => update("distance", Number.parseInt(event.target.value, 10))} />
      </form>
    </RouteShell>
  );
}

function JoinGame() {
  const [host, setHost] = useState("127.0.0.1");
  const [port, setPort] = useState("27015");
  return (
    <RouteShell
      title="Join Game"
      subtitle="Enter the host address and UDP port."
      footer={
        <div className="action-stack">
          <button className="action-button" type="submit" form="join-form">Connect</button>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <form id="join-form" onSubmit={(event) => { event.preventDefault(); sendAction("join", { primary: host, secondary: port }); }}>
        <div className="field-grid">
          <label className="field-row">
            <span className="field-label">Host address</span>
            <input value={host} onChange={(event) => setHost(event.target.value)} required />
          </label>
          <label className="field-row">
            <span className="field-label">UDP port</span>
            <input type="number" min="1" max="65535" value={port} onChange={(event) => setPort(event.target.value)} required />
          </label>
        </div>
      </form>
    </RouteShell>
  );
}

function Loading({ model }) {
  const percent = Math.round((model.progress || 0) * 100);
  return (
    <RouteShell title={model.title || "Loading"} subtitle={model.message || "Preparing your world..."} blocking>
      <progress value={model.progress} max="1">{percent}%</progress>
      <p className="supporting" style={{ marginTop: 10, textAlign: "right" }}>{percent}%</p>
    </RouteShell>
  );
}

function ErrorRoute({ model }) {
  return (
    <RouteShell
      title={model.title || "Something went wrong"}
      blocking
      footer={<ActionButton kind="acknowledge-error">Return to Main Menu</ActionButton>}
    >
      <p className="error-copy" role="alert">{model.message}</p>
    </RouteShell>
  );
}

function Controls() {
  const bindings = [
    ["Move", "W A S D"],
    ["Look", "Mouse"],
    ["Jump", "Space"],
    ["Break / Place", "Left / Right Click"],
    ["Pause", "Escape"]
  ];
  return (
    <RouteShell title="Controls" footer={<ActionButton kind="dismiss-controls">Got it</ActionButton>}>
      <dl className="controls-grid">
        {bindings.map(([action, keys]) => (
          <React.Fragment key={action}>
            <dt>{action}</dt>
            <dd>{keys}</dd>
          </React.Fragment>
        ))}
      </dl>
    </RouteShell>
  );
}

function SliderField({ label, value, min, max, step = 1, onChange, format }) {
  return (
    <>
      <div className="slider-row">
        <span className="field-label">{label}</span>
        <span className="slider-value">{format ? format(value) : value}</span>
      </div>
      <input type="range" min={min} max={max} step={step} value={value} onChange={(event) => onChange(Number.parseFloat(event.target.value))} />
    </>
  );
}

function Settings() {
  const [settings, setSettings] = useState({ fov: 80, renderDistance: 8, simulationDistance: 6, master: 1, music: 1, effects: 1, sensitivity: 1, invertY: false, particles: true });
  const update = (key, value) => setSettings({ ...settings, [key]: value });
  const percent = (value) => `${Math.round(value * 100)}%`;
  return (
    <RouteShell
      title="Settings"
      wide
      footer={
        <div className="action-stack">
          <button className="action-button" type="submit" form="settings-form">Apply</button>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <form id="settings-form" onSubmit={(event) => { event.preventDefault(); sendAction("apply-settings", { secondary: JSON.stringify(settings) }); }}>
        <p className="section-title">Video</p>
        <SliderField label="Field of view" value={settings.fov} min={60} max={110} onChange={(value) => update("fov", value)} format={(value) => `${value}°`} />
        <SliderField label="Render distance" value={settings.renderDistance} min={2} max={16} onChange={(value) => update("renderDistance", Math.round(value))} format={(value) => `${value} chunks`} />
        <SliderField label="Simulation distance" value={settings.simulationDistance} min={2} max={12} onChange={(value) => update("simulationDistance", Math.round(value))} format={(value) => `${value} chunks`} />

        <p className="section-title">Audio</p>
        <SliderField label="Master volume" value={settings.master} min={0} max={1} step={0.01} onChange={(value) => update("master", value)} format={percent} />
        <SliderField label="Music volume" value={settings.music} min={0} max={1} step={0.01} onChange={(value) => update("music", value)} format={percent} />
        <SliderField label="Effects volume" value={settings.effects} min={0} max={1} step={0.01} onChange={(value) => update("effects", value)} format={percent} />

        <p className="section-title">Controls</p>
        <SliderField label="Mouse sensitivity" value={settings.sensitivity} min={0.1} max={4} step={0.1} onChange={(value) => update("sensitivity", value)} format={(value) => value.toFixed(1)} />
        <div className="toggle-grid" style={{ marginTop: 4 }}>
          <Chip checked={settings.invertY} onChange={(value) => update("invertY", value)}>Invert Y</Chip>
          <Chip checked={settings.particles} onChange={(value) => update("particles", value)}>Particles</Chip>
        </div>
      </form>
    </RouteShell>
  );
}

function App() {
  const [model, setModel] = useState({ route: Route.MainMenu, revision: 0, progress: 0, items: [] });
  useEffect(() => {
    window.__voxelsReceiveModel = setModel;
    if (window.__voxelsLastModel) setModel(window.__voxelsLastModel);
    return () => delete window.__voxelsReceiveModel;
  }, []);
  return (
    <>
      <SceneBackdrop />
      {(() => {
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
      })()}
    </>
  );
}

createRoot(document.getElementById("root")).render(<App />);
