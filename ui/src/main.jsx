import React, { useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import "./styles.css";
import { Route, parseJson, sendUiAction, useVoxelsActionGate, useVoxelsBridgeModel } from "./lib/voxelsBridge";

const splashLogo = "/assets/studio-logo.png";

function ActionButton({ children, kind, fields, className = "", style, ...props }) {
  return (
    <button className={`action-button ${className}`} type="button" style={style} onClick={() => sendUiAction(kind, fields)} {...props}>
      {children}
    </button>
  );
}

function SplashIntro({ model }) {
  const payload = parseJson(model?.payload, null);
  const fade = typeof payload?.fade === "number" ? Math.max(0, Math.min(1, payload.fade)) :
    (typeof model?.progress === "number" ? Math.max(0, Math.min(1, model.progress)) : 1);
  const [logoMissing, setLogoMissing] = useState(false);
  return (
    <main className="player-ui" aria-busy>
      <section className="splash-panel" style={{ opacity: fade }} aria-label="Startup preface">
        {!logoMissing ? (
          <img className="splash-logo" src={splashLogo} alt="Fractal Dynamics" onError={() => setLogoMissing(true)} />
        ) : null}
        <p className="supporting">{model?.message || "(c) 2026 Fractal Dynamics, All rights reserved."}</p>
      </section>
    </main>
  );
}

function MenuAtmosphere({ opacity = 1 }) {
  return <div className="menu-atmosphere" style={{ opacity }} aria-hidden="true" />;
}

/** Shared frame for every route except the main menu's own hero layout. */
function RouteShell({ title, kicker = "Voxels Engine", subtitle, children, footer, blocking = false, wide = false, autoFocusHeading = true }) {
  const heading = useRef(null);
  useEffect(() => {
    if (!autoFocusHeading) return;
    heading.current?.focus();
  }, [title, autoFocusHeading]);
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
  const { runGuardedAction, isPending } = useVoxelsActionGate();
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
          <button className="action-button" type="submit" form="world-creation-form" disabled={isPending("create-world")}>Create World</button>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <form id="world-creation-form" onSubmit={(event) => {
        event.preventDefault();
        runGuardedAction("create-world", "create-world", {
          primary: name,
          secondary: JSON.stringify({ seed, ...options })
        });
      }}>
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
  const { runGuardedAction, isPending } = useVoxelsActionGate();
  const [host, setHost] = useState("127.0.0.1");
  const [port, setPort] = useState("27015");
  return (
    <RouteShell
      title="Join Game"
      subtitle="Enter the host address and UDP port."
      footer={
        <div className="action-stack">
          <button className="action-button" type="submit" form="join-form" disabled={isPending("join")}>Connect</button>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <form id="join-form" onSubmit={(event) => {
        event.preventDefault();
        runGuardedAction("join", "join", { primary: host, secondary: port });
      }}>
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
  const payload = parseJson(model?.payload, null);
  const backdropOpacity = typeof payload?.backdropOpacity === "number"
    ? Math.max(0, Math.min(1, payload.backdropOpacity))
    : 0;
  const showStatus = payload?.showStatus !== false;
  const percent = Math.round((model.progress || 0) * 100);
  return (
    <>
      <div className="loading-opaque-layer" style={{ opacity: backdropOpacity }} aria-hidden="true" />
      {showStatus ? (
        <RouteShell title={model.title || "Loading"} subtitle={model.message || "Preparing your world..."} blocking autoFocusHeading={false}>
          <progress value={model.progress} max="1">{percent}%</progress>
          <p className="supporting" style={{ marginTop: 10, textAlign: "right" }}>{percent}%</p>
        </RouteShell>
      ) : null}
    </>
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

function OptionPicker({ label, valueLabel, onPrevious, onNext }) {
  return (
    <div className="field-row">
      <span className="field-label">{label}</span>
      <div className="option-picker" role="group" aria-label={label}>
        <button className="option-picker-button" type="button" onClick={onPrevious} aria-label={`Previous ${label}`}>
          &lt;
        </button>
        <span className="option-picker-value">{valueLabel}</span>
        <button className="option-picker-button" type="button" onClick={onNext} aria-label={`Next ${label}`}>
          &gt;
        </button>
      </div>
    </div>
  );
}

const WINDOW_MODES = ["Windowed", "Borderless", "Fullscreen"];
const RESOLUTION_PRESETS = [
  { width: 1280, height: 720, aspect: "16:9", label: "1280 x 720 (16:9)" },
  { width: 1366, height: 768, aspect: "16:9", label: "1366 x 768 (16:9)" },
  { width: 1600, height: 900, aspect: "16:9", label: "1600 x 900 (16:9)" },
  { width: 1920, height: 1080, aspect: "16:9", label: "1920 x 1080 (16:9)" },
  { width: 2560, height: 1440, aspect: "16:9", label: "2560 x 1440 (16:9)" },
  { width: 3840, height: 2160, aspect: "16:9", label: "3840 x 2160 (16:9)" }
];

function ResolutionKey(width, height) {
  return `${width}x${height}`;
}

function Settings({ model }) {
  const { runGuardedAction, isPending } = useVoxelsActionGate();
  const [settings, setSettings] = useState({
    windowMode: "Windowed",
    resolutionWidth: 1280,
    resolutionHeight: 720,
    fov: 90,
    renderDistance: 8,
    simulationDistance: 4,
    master: 1,
    music: 0.7,
    effects: 0.8,
    sensitivity: 1,
    invertY: false,
    particles: true
  });
  useEffect(() => {
    const payload = parseJson(model?.payload, null);
    if (!payload || typeof payload !== "object") return;
    setSettings((previous) => ({
      ...previous,
      windowMode: typeof payload.windowMode === "string" ? payload.windowMode : previous.windowMode,
      resolutionWidth: typeof payload.resolutionWidth === "number" ? payload.resolutionWidth : previous.resolutionWidth,
      resolutionHeight: typeof payload.resolutionHeight === "number" ? payload.resolutionHeight : previous.resolutionHeight,
      fov: typeof payload.fov === "number" ? payload.fov : previous.fov,
      renderDistance: typeof payload.renderDistance === "number" ? payload.renderDistance : previous.renderDistance,
      simulationDistance: typeof payload.simulationDistance === "number" ? payload.simulationDistance : previous.simulationDistance,
      master: typeof payload.master === "number" ? payload.master : previous.master,
      music: typeof payload.music === "number" ? payload.music : previous.music,
      effects: typeof payload.effects === "number" ? payload.effects : previous.effects,
      sensitivity: typeof payload.sensitivity === "number" ? payload.sensitivity : previous.sensitivity,
      invertY: typeof payload.invertY === "boolean" ? payload.invertY : previous.invertY,
      particles: typeof payload.particles === "boolean" ? payload.particles : previous.particles
    }));
  }, [model?.revision, model?.payload]);
  const update = (key, value) => setSettings({ ...settings, [key]: value });
  const percent = (value) => `${Math.round(value * 100)}%`;
  const selectedResolution = ResolutionKey(settings.resolutionWidth, settings.resolutionHeight);
  const modeIndex = Math.max(0, WINDOW_MODES.indexOf(settings.windowMode));
  const resolutionIndex = Math.max(0, RESOLUTION_PRESETS.findIndex((resolution) => ResolutionKey(resolution.width, resolution.height) === selectedResolution));
  const currentResolution = RESOLUTION_PRESETS[resolutionIndex] || RESOLUTION_PRESETS[0];

  const stepWindowMode = (delta) => {
    const nextIndex = (modeIndex + delta + WINDOW_MODES.length) % WINDOW_MODES.length;
    setSettings({ ...settings, windowMode: WINDOW_MODES[nextIndex] });
  };

  const stepResolution = (delta) => {
    const nextIndex = (resolutionIndex + delta + RESOLUTION_PRESETS.length) % RESOLUTION_PRESETS.length;
    const nextResolution = RESOLUTION_PRESETS[nextIndex];
    setSettings({ ...settings, resolutionWidth: nextResolution.width, resolutionHeight: nextResolution.height });
  };

  return (
    <RouteShell
      title="Settings"
      wide
      footer={
        <div className="action-stack">
          <button className="action-button" type="submit" form="settings-form" disabled={isPending("settings-apply")}>Apply</button>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <form id="settings-form" onSubmit={(event) => {
        event.preventDefault();
        runGuardedAction("settings-apply", "apply-settings", {
          secondary: JSON.stringify(settings),
          settings
        });
      }}>
        <p className="section-title">Video</p>
        <OptionPicker
          label="Window mode"
          valueLabel={WINDOW_MODES[modeIndex]}
          onPrevious={() => stepWindowMode(-1)}
          onNext={() => stepWindowMode(1)}
        />
        <OptionPicker
          label="Resolution"
          valueLabel={currentResolution.label}
          onPrevious={() => stepResolution(-1)}
          onNext={() => stepResolution(1)}
        />
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
  const [model] = useVoxelsBridgeModel({ route: Route.MainMenu, revision: 0, progress: 0, items: [] });
  const showAtmosphere = model.route !== Route.Hud && model.route !== Route.Pause;

  // Example usage for future gameplay overlays:
  // const { runGuardedAction } = useVoxelsActionGate();
  // runGuardedAction("pause-resume", "resume", {});
  return (
    <>
      {showAtmosphere ? <MenuAtmosphere opacity={model.route === Route.Loading ? 0.9 : 1} /> : null}
      {(() => {
        switch (model.route) {
          case Route.Splash: return <SplashIntro model={model} />;
          case Route.MainMenu: return <MainMenu />;
          case Route.SaveSelection: return <WorldSelect model={model} />;
          case Route.WorldCreation: return <WorldCreation />;
          case Route.Loading: return <Loading model={model} />;
          case Route.Join: return <JoinGame />;
          case Route.Error: return <ErrorRoute model={model} />;
          case Route.FatalError: return <ErrorRoute model={model} />;
          case Route.Hud: return null;
          case Route.Pause: return null;
          case Route.Settings: return <Settings model={model} />;
          case Route.ControlsCard: return <Controls />;
          default: return null;
        }
      })()}
    </>
  );
}

createRoot(document.getElementById("root")).render(<App />);
