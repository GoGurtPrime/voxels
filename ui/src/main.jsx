import React, { useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import "./styles.css";
import { Route, parseJson, sendUiAction, useVoxelsActionGate, useVoxelsBridgeModel } from "./lib/voxelsBridge";
import iconPlanks from "./assets/recipes/planks.svg";
import iconGardenMix from "./assets/recipes/garden_mix.svg";
import iconStoneAxe from "./assets/recipes/stone_axe.svg";
import iconStick from "./assets/recipes/stick.svg";

const splashLogo = "/assets/studio-logo.png";

function ActionButton({ children, kind, fields, className = "", style, onClick, ...props }) {
  return (
    <button className={`action-button ${className}`} type="button" style={style} onClick={onClick || (() => sendUiAction(kind, fields))} {...props}>
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

function useDirectionalNavigation(route) {
  useEffect(() => {
    const handleKeyDown = (event) => {
      if (event.key === "Escape") {
        if (route === Route.Pause) sendUiAction("resume");
        else if (![Route.MainMenu, Route.Loading, Route.Hud, Route.Splash].includes(route)) sendUiAction("back");
        return;
      }
      if (!["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"].includes(event.key)) return;
      const target = event.target;
      if ((target instanceof HTMLInputElement && ["text", "number", "range"].includes(target.type)) &&
          ["ArrowLeft", "ArrowRight"].includes(event.key)) return;
      const controls = [...document.querySelectorAll("button:not(:disabled), input:not(:disabled), [tabindex]:not([tabindex='-1'])")]
        .filter((element) => element.getClientRects().length > 0);
      if (!controls.length) return;
      const current = controls.indexOf(document.activeElement);
      const delta = ["ArrowDown", "ArrowRight"].includes(event.key) ? 1 : -1;
      const next = current < 0 ? 0 : (current + delta + controls.length) % controls.length;
      event.preventDefault();
      controls[next].focus();
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [route]);
}

/** Shared frame for every route except the main menu's own hero layout. */
function RouteShell({ title, kicker = "Voxels Engine", subtitle, children, footer, blocking = false, wide = false, autoFocusHeading = true, className = "" }) {
  const heading = useRef(null);
  useEffect(() => {
    if (!autoFocusHeading) return;
    heading.current?.focus();
  }, [title, autoFocusHeading]);
  return (
    <main className="player-ui" aria-busy={blocking} style={{ alignItems: "center", justifyContent: "center", padding: 24 }}>
      <section className={`glass-panel route-shell anim-rise ${wide ? "route-shell-wide" : ""} ${className}`} aria-labelledby="route-title">
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

function MainMenu({ model }) {
  const payload = parseJson(model?.payload, {});
  const titleOpacity = typeof payload?.titleOpacity === "number"
    ? Math.max(0, Math.min(1, payload.titleOpacity))
    : 1;
  const showMenuButtons = payload?.showMenuButtons !== false;
  const items = [
    { kind: "play", label: "Play", className: "" },
    { kind: "join", label: "Join Game", className: "quiet" },
    { kind: "settings", label: "Settings", className: "quiet" },
    { kind: "quit", label: "Quit", className: "ghost" }
  ];
  return (
    <main className="player-ui">
      <div className="hero-shell">
        <div className="hero-title-block" style={{ opacity: titleOpacity }}>
          <p className="kicker">Voxels Engine</p>
          <h1 className="hero-title anim-glow">Voxels</h1>
          <p className="hero-subtitle">A block-based world is waiting.</p>
        </div>
        {showMenuButtons ? (
          <>
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
          </>
        ) : null}
      </div>
    </main>
  );
}

function WorldSelect({ model }) {
  const [selected, setSelected] = useState("");
  const [confirmation, setConfirmation] = useState("");
  const [confirmingDelete, setConfirmingDelete] = useState(false);
  const [previewFailed, setPreviewFailed] = useState(false);
  const [hasMoreWorlds, setHasMoreWorlds] = useState(false);
  const worldListRef = useRef(null);
  const payload = parseJson(model.payload, {});
  const fallbackWorlds = model.items.filter((item) => item.startsWith("save:")).map((item) => {
    const [slot, name] = item.slice(5).split("|");
    return { slot, name: name || slot };
  });
  const worlds = Array.isArray(payload.worlds) ? payload.worlds : fallbackWorlds;
  const target = selected || worlds[0]?.slot || "";
  const selectedWorld = worlds.find((world) => world.slot === target);
  const selectedStillExists = !selected || worlds.some((world) => world.slot === selected);
  useEffect(() => setPreviewFailed(false), [selectedWorld?.previewUrl]);
  useEffect(() => {
    if (!confirmingDelete || selectedStillExists) return;
    setConfirmingDelete(false);
    setConfirmation("");
    setSelected("");
  }, [confirmingDelete, selectedStillExists]);
  const refreshWorldListIndicator = () => {
    const list = worldListRef.current;
    setHasMoreWorlds(Boolean(list && list.scrollTop + list.clientHeight < list.scrollHeight - 1));
  };
  useEffect(() => {
    const list = worldListRef.current;
    if (!list) return undefined;
    refreshWorldListIndicator();
    const observer = new ResizeObserver(refreshWorldListIndicator);
    observer.observe(list);
    return () => observer.disconnect();
  }, [worlds.length, confirmingDelete]);

  if (confirmingDelete && selectedWorld) {
    return (
      <RouteShell
        title="Delete World?"
        kicker="Permanent action"
        subtitle={`This will permanently remove ${selectedWorld.name}.`}
        autoFocusHeading={false}
        footer={
          <div className="action-row">
            <ActionButton className="danger" kind="confirm-delete" fields={{ primary: target, secondary: confirmation }} disabled={confirmation !== selectedWorld.name}>Delete Forever</ActionButton>
            <ActionButton className="ghost" onClick={() => { setConfirmingDelete(false); setConfirmation(""); }}>Cancel</ActionButton>
          </div>
        }
      >
        <label className="field-row">
          <span className="field-label">Type {selectedWorld.name} to confirm (case-sensitive)</span>
          <input type="text" value={confirmation} maxLength={48} onChange={(event) => setConfirmation(event.target.value)} autoComplete="off" spellCheck={false} autoFocus />
        </label>
      </RouteShell>
    );
  }

  return (
    <RouteShell
      title="Select World"
      subtitle="Choose a world, inspect its details, then continue your journey."
      wide
      className="world-select-shell"
      footer={
        <div className="action-row">
          <ActionButton kind="create-world">New World</ActionButton>
          <ActionButton kind="load-world" fields={{ primary: target }} disabled={!target}>Play Selected</ActionButton>
          <ActionButton className="danger quiet-danger" onClick={() => { setSelected(target); setConfirmingDelete(true); }} disabled={!target}>Delete</ActionButton>
          <ActionButton className="ghost" kind="back">Back</ActionButton>
        </div>
      }
    >
      <div className="world-browser">
        <div className="world-list-frame">
          <section className="world-list" aria-label="Saved worlds" ref={worldListRef} onScroll={refreshWorldListIndicator}>
            <p className="section-title">Saved Worlds</p>
            {worlds.length ? worlds.map((world) => (
              <button
                className={`world-option ${target === world.slot ? "selected" : ""}`}
                key={world.slot}
                type="button"
                aria-pressed={target === world.slot}
                onClick={() => setSelected(world.slot)}
              >
                <span className="world-marker" aria-hidden="true" />
                <span>
                  <strong className="world-name">{world.name}</strong>
                  <span className="world-mode">{world.mode || "World save"}</span>
                </span>
              </button>
            )) : (
              <div className="empty-worlds">
                <p className="section-title">No saved worlds</p>
                <p className="supporting">Create your first world to begin exploring.</p>
              </div>
            )}
          </section>
          {hasMoreWorlds ? <span className="world-list-more" aria-hidden="true" /> : null}
        </div>
        <section className="world-details" aria-live="polite">
          <div className="world-preview-window" role="img" aria-label={selectedWorld ? `World preview for ${selectedWorld.name}` : "World preview"}>
            {selectedWorld?.previewUrl && !previewFailed ? (
              <img src={selectedWorld.previewUrl} alt="" aria-hidden="true" onError={() => setPreviewFailed(true)} />
            ) : null}
            <span>{selectedWorld ? selectedWorld.name : "A new horizon"}</span>
          </div>
          {selectedWorld ? (
            <dl className="world-metadata">
              <div><dt>Mode</dt><dd>{selectedWorld.mode || "Survival"}</dd></div>
              <div><dt>Seed</dt><dd>{selectedWorld.seed ?? "Unknown"}</dd></div>
              <div><dt>Created</dt><dd>{selectedWorld.createdUtc || "Unknown"}</dd></div>
              <div><dt>Last played</dt><dd>{selectedWorld.lastPlayedAt || "Never"}</dd></div>
              <div><dt>Play time</dt><dd>{Math.floor((selectedWorld.playTimeSeconds || 0) / 60)} min</dd></div>
              <div><dt>Access</dt><dd>{selectedWorld.public ? "Public LAN" : "Private"}</dd></div>
            </dl>
          ) : <p className="supporting">Select or create a world to see its details.</p>}
        </section>
      </div>
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
      autoFocusHeading={false}
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
            <span className="field-heading"><span className="field-label">World name</span><output>{name.length}/48</output></span>
            <input type="text" value={name} maxLength={48} onChange={(event) => setName(event.target.value.replace(/[^A-Za-z0-9 _-]/g, "").slice(0, 48))} autoComplete="off" spellCheck={false} required autoFocus />
          </label>
          <label className="field-row">
            <span className="field-heading"><span className="field-label">Seed <span className="optional">optional</span></span><output>{seed.length}/20</output></span>
            <input type="text" value={seed} maxLength={20} onChange={(event) => setSeed(event.target.value.replace(/[^A-Za-z0-9]/g, "").slice(0, 20))} inputMode="text" autoComplete="off" spellCheck={false} />
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
      autoFocusHeading={false}
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
            <input type="text" value={host} maxLength={255} onChange={(event) => setHost(event.target.value)} inputMode="url" autoComplete="off" autoCapitalize="none" spellCheck={false} required autoFocus />
          </label>
          <label className="field-row">
            <span className="field-label">UDP port</span>
            <input type="text" value={port} maxLength={5} onChange={(event) => setPort(event.target.value.replace(/\D/g, "").slice(0, 5))} inputMode="numeric" pattern="[0-9]*" autoComplete="off" required />
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
  const clampValue = (next) => onChange(Math.min(max, Math.max(min, next)));
  return (
    <div className="slider-control">
      <div className="slider-row">
        <span className="field-label">{label}</span>
        <span className="slider-value">{format ? format(value) : value}</span>
      </div>
      <div className="slider-input-row">
        <button type="button" onClick={() => clampValue(value - step)} aria-label={`Decrease ${label}`}>-</button>
        <input type="range" min={min} max={max} step={step} value={value} onChange={(event) => onChange(Number.parseFloat(event.target.value))} />
        <button type="button" onClick={() => clampValue(value + step)} aria-label={`Increase ${label}`}>+</button>
      </div>
    </div>
  );
}

function PauseMenu({ model }) {
  const payload = parseJson(model?.payload, {});
  const [isPublic, setIsPublic] = useState(Boolean(payload.public));
  useEffect(() => setIsPublic(Boolean(payload.public)), [model?.revision, payload.public]);
  const remote = Boolean(payload.remote);
  const toggleVisibility = () => {
    const next = !isPublic;
    setIsPublic(next);
    sendUiAction("toggle-world-visibility", { value: next ? 1 : 0 });
  };
  return (
    <RouteShell
      title="Paused"
      kicker={remote ? "Connected session" : "World suspended"}
      subtitle={remote ? "The remote world remains connected while this menu is open." : "Your local world is held while you choose what comes next."}
      className="pause-shell"
    >
      <div className="pause-layout">
        <nav className="pause-actions" aria-label="Pause menu">
          <ActionButton kind="resume" autoFocus>Resume</ActionButton>
          <ActionButton className="quiet" kind="settings">Settings</ActionButton>
          <ActionButton className="quiet" kind="controls">Controls</ActionButton>
          {!remote ? <ActionButton className="quiet" onClick={toggleVisibility}>{isPublic ? "Set World Private" : "Set World Public"}</ActionButton> : null}
          <ActionButton className="ghost" kind="return-to-main-menu">{remote ? "Leave Server" : "Save and Quit to Menu"}</ActionButton>
          <ActionButton className="danger quiet-danger" kind="exit-to-desktop">{remote ? "Leave and Exit" : "Save and Exit"}</ActionButton>
        </nav>
        <aside className="session-details">
          <p className="section-title">Session</p>
          <dl className="world-metadata">
            <div><dt>Type</dt><dd>{remote ? "Remote" : "Local host"}</dd></div>
            {!remote ? <div><dt>Access</dt><dd>{isPublic ? "Public LAN" : "Private"}</dd></div> : null}
            {payload.hosting ? <div><dt>UDP port</dt><dd>{payload.port}</dd></div> : null}
          </dl>
        </aside>
      </div>
    </RouteShell>
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
    particles: true,
    crosshairSize: 1,
    crosshairHighContrast: false,
    reducedMotion: false
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
      particles: typeof payload.particles === "boolean" ? payload.particles : previous.particles,
      crosshairSize: typeof payload.crosshairSize === "number" ? payload.crosshairSize : previous.crosshairSize,
      crosshairHighContrast: typeof payload.crosshairHighContrast === "boolean" ? payload.crosshairHighContrast : previous.crosshairHighContrast,
      reducedMotion: typeof payload.reducedMotion === "boolean" ? payload.reducedMotion : previous.reducedMotion
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
        <SliderField label="Crosshair size" value={settings.crosshairSize} min={0.5} max={2} step={0.1} onChange={(value) => update("crosshairSize", value)} format={(value) => `${value.toFixed(1)}x`} />
        <div className="toggle-grid" style={{ marginTop: 4 }}>
          <Chip checked={settings.invertY} onChange={(value) => update("invertY", value)}>Invert Y</Chip>
          <Chip checked={settings.particles} onChange={(value) => update("particles", value)}>Particles</Chip>
          <Chip checked={settings.crosshairHighContrast} onChange={(value) => update("crosshairHighContrast", value)}>High contrast crosshair</Chip>
          <Chip checked={settings.reducedMotion} onChange={(value) => update("reducedMotion", value)}>Reduced motion</Chip>
        </div>
      </form>
    </RouteShell>
  );
}

const RECIPE_ICONS = {
  planks: iconPlanks,
  garden_mix: iconGardenMix,
  stone_axe: iconStoneAxe,
  stick: iconStick
};

/** One inventory/hotbar cell: drag-and-drop reorder, click-to-select (hotbar only), and a
 * hover/focus-revealed drop control. Square at every viewport size instead of a squished bar. */
function InventorySlot({ slot, selectable, dragSlot, setDragSlot }) {
  const hasItem = Number(slot.count) > 0;
  const index = Number(slot.slot);
  const isDragTarget = dragSlot !== null && dragSlot !== index;
  return (
    <button
      type="button"
      className={`hud-slot ${slot.selected ? "selected" : ""} ${hasItem ? "" : "empty"}`}
      aria-pressed={selectable ? Boolean(slot.selected) : undefined}
      aria-label={hasItem ? `${slot.name}, ${slot.count}${selectable ? ", press to select" : ""}` : "Empty slot"}
      draggable={hasItem}
      onDragStart={(event) => {
        if (!hasItem) { event.preventDefault(); return; }
        event.dataTransfer.setData("text/plain", String(index));
        event.dataTransfer.effectAllowed = "move";
        setDragSlot(index);
      }}
      onDragEnd={() => setDragSlot(null)}
      onDragOver={(event) => event.preventDefault()}
      onDrop={(event) => {
        event.preventDefault();
        const from = Number(event.dataTransfer.getData("text/plain"));
        setDragSlot(null);
        if (!Number.isFinite(from) || from === index) return;
        sendUiAction("move-item", { value: from, primary: String(index) });
      }}
      onClick={() => { if (selectable) sendUiAction("hotbar", { value: index }); }}
    >
      {selectable ? <span className="hud-slot-number">{index + 1}</span> : null}
      <span className="hud-slot-name">{slot.name || ""}</span>
      {hasItem ? <span className="hud-slot-count">{slot.count}</span> : null}
      {hasItem ? (
        <span
          role="button"
          tabIndex={-1}
          className="hud-slot-drop"
          title="Drop"
          aria-hidden="true"
          onClick={(event) => {
            event.stopPropagation();
            sendUiAction("drop-item", { value: index });
          }}
        >
          ×
        </span>
      ) : null}
      {isDragTarget ? <span className="hud-slot-drag-hint" aria-hidden="true" /> : null}
    </button>
  );
}

/** Keyboard vs controller button-prompt bar; swaps glyph style from InputManager's
 * last-active-device signal (payload.inputMethod) so prompts always match what the player is
 * actually holding, ready for a future auto-detected-controller pass. */
function ControlHints({ inputMethod, craftingOpen }) {
  const hints = craftingOpen
    ? [
        [inputMethod === "gamepad" ? "LS" : "Drag", "Move item"],
        [inputMethod === "gamepad" ? "X" : "Click ×", "Drop item"],
        [inputMethod === "gamepad" ? "B" : "Esc", "Close"]
      ]
    : [
        [inputMethod === "gamepad" ? "LS" : "WASD", "Move"],
        [inputMethod === "gamepad" ? "A" : "Space", "Jump"],
        [inputMethod === "gamepad" ? "RT" : "LMB", "Break"],
        [inputMethod === "gamepad" ? "LT" : "RMB", "Place"],
        [inputMethod === "gamepad" ? "Y" : "E", "Inventory"],
        [inputMethod === "gamepad" ? "RB/LB" : "G", "Drop"]
      ];
  return (
    <div className={`hud-control-hints ${inputMethod === "gamepad" ? "gamepad" : "keyboard"}`} aria-hidden="true">
      {hints.map(([glyph, label]) => (
        <span className="hud-control-hint" key={label}>
          <span className="hud-control-glyph">{glyph}</span>
          <span className="hud-control-label">{label}</span>
        </span>
      ))}
    </div>
  );
}

function HudRoute({ model }) {
  const payload = parseJson(model?.payload, {}) || {};
  const hotbar = Array.isArray(payload.hotbar) ? payload.hotbar : [];
  const inventorySlots = Array.isArray(payload?.inventory?.slots) ? payload.inventory.slots : [];
  const notifications = Array.isArray(payload.notifications) ? payload.notifications : [];
  const recipes = Array.isArray(payload?.crafting?.recipes) ? payload.crafting.recipes : [];
  const inputMethod = payload.inputMethod === "gamepad" ? "gamepad" : "keyboard";
  const [chatDraft, setChatDraft] = useState("");
  const [recipeTab, setRecipeTab] = useState("all");
  const [dragSlot, setDragSlot] = useState(null);
  const chatOpen = Boolean(payload?.chat?.open);
  const craftingOpen = Boolean(payload?.crafting?.open);
  const crosshairSize = Math.max(0.5, Math.min(2, Number(payload?.crosshair?.size || 1)));
  const crosshairHighContrast = Boolean(payload?.crosshair?.highContrast);
  const reducedMotion = Boolean(payload?.crosshair?.reducedMotion);
  const targetName = payload?.target?.hit ? payload?.target?.name || "Target" : "";
  const orderedInventory = [...inventorySlots].sort((a, b) => Number(a.slot) - Number(b.slot));
  const mainSlots = orderedInventory.filter((slot) => !slot.hotbar);
  const hotbarSlots = orderedInventory.filter((slot) => slot.hotbar);
  const displayHotbar = hotbarSlots.length ? hotbarSlots : hotbar;
  const inventoryByName = orderedInventory.reduce((map, slot) => {
    const name = String(slot?.name || "").trim().toLowerCase();
    const count = Number(slot?.count || 0);
    if (!name || count <= 0) return map;
    map.set(name, (map.get(name) || 0) + count);
    return map;
  }, new Map());

  useEffect(() => {
    const onKeyDown = (event) => {
      if (event.key !== "Escape") return;
      if (chatOpen) {
        sendUiAction("close-chat");
        return;
      }
      if (craftingOpen) {
        sendUiAction("close-crafting");
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [chatOpen, craftingOpen]);

  const categories = [
    ["all", "All"],
    ["construction", "Construction"],
    ["food", "Food"],
    ["tools", "Tools"],
    ["furniture", "Furniture"],
    ["smelters", "Smelters"]
  ];
  const filteredRecipes = recipes
    .filter((recipe) => recipeTab === "all" || recipe.category === recipeTab)
    .sort((a, b) => String(a.sort || a.name || "").localeCompare(String(b.sort || b.name || "")));

  const canCraftRecipe = (recipe) => {
    if (!Array.isArray(recipe?.ingredients)) return false;
    return recipe.ingredients.every((ingredient) => {
      const needed = Number(ingredient?.count || 0);
      const key = String(ingredient?.name || "").trim().toLowerCase();
      if (!key || needed <= 0) return true;
      return (inventoryByName.get(key) || 0) >= needed;
    });
  };

  const overlayOpen = chatOpen || craftingOpen;

  return (
    <main className={`hud-root ${craftingOpen ? "overlay-open" : ""}`} aria-live="polite">
      {!overlayOpen ? (
        <div className={`hud-crosshair ${crosshairHighContrast ? "high-contrast" : ""}`} style={{ "--crosshair-size": `${crosshairSize}` }} data-reduced-motion={reducedMotion ? "true" : "false"}>
          <span />
          <span />
        </div>
      ) : null}

      {!overlayOpen && targetName ? <div className="hud-target">{targetName}</div> : null}

      {!overlayOpen ? (
        <aside className="hud-feed glass-panel" aria-label="Session events">
          <div className="hud-feed-header">
            <span>Events</span>
            <button type="button" className="hud-utility" onClick={() => sendUiAction("open-chat")}>Chat (T)</button>
            <button type="button" className="hud-utility" onClick={() => sendUiAction("open-crafting")}>Inventory & Crafting (E)</button>
          </div>
          <div className="hud-feed-log" role="log" aria-label="Session messages">
            {notifications.length ? notifications.map((entry) => <p key={entry.id}>{entry.text}</p>) : <p className="quiet">No messages yet.</p>}
          </div>
        </aside>
      ) : null}

      {chatOpen ? (
        <section className="glass-panel hud-chat-panel" aria-label="Chat input">
          <header className="hud-overlay-header">
            <div>
              <p className="kicker">Player Chat</p>
              <h2>Session Messages</h2>
            </div>
            <button type="button" className="action-button ghost" onClick={() => sendUiAction("close-chat")}>Close</button>
          </header>
          <div className="hud-chat-log" role="log" aria-label="Recent chat and notifications">
            {notifications.length ? notifications.map((entry) => <p key={entry.id}>{entry.text}</p>) : <p className="quiet">No messages yet.</p>}
          </div>
          <form
            className="hud-chat-form"
            onSubmit={(event) => {
              event.preventDefault();
              if (!chatDraft.trim()) return;
              sendUiAction("send-chat", { primary: chatDraft.trim() });
              setChatDraft("");
            }}
          >
            <label>
              Message
              <input type="text" maxLength={120} value={chatDraft} onChange={(event) => setChatDraft(event.target.value)} autoFocus />
            </label>
            <div className="hud-chat-actions">
              <button type="submit" className="action-button quiet">Send</button>
              <button type="button" className="action-button ghost" onClick={() => sendUiAction("close-chat")}>Cancel</button>
            </div>
          </form>
        </section>
      ) : null}

      {craftingOpen ? (
        <section className="hud-overlay-shell glass-panel hud-crafting-modal" aria-label="Crafting and inventory">
          <header className="hud-overlay-header">
            <div>
              <p className="kicker">Paused Interaction</p>
              <h2>Inventory & Crafting</h2>
            </div>
            <button type="button" className="action-button ghost" onClick={() => sendUiAction("close-crafting")}>Close</button>
          </header>

          <div className="hud-crafting-layout">
            <section className="hud-recipes" aria-label="Crafting recipes">
              <div className="hud-crafting-tabs" role="tablist" aria-label="Recipe categories">
                {categories.map(([value, label]) => (
                  <button key={value} type="button" role="tab" aria-selected={recipeTab === value} className={recipeTab === value ? "active" : ""} onClick={() => setRecipeTab(value)}>{label}</button>
                ))}
              </div>
              <div className="hud-recipe-list">
                {filteredRecipes.map((recipe) => {
                  const canCraft = canCraftRecipe(recipe);
                  return (
                    <button key={recipe.id} type="button" className={`hud-recipe ${canCraft ? "" : "disabled"}`} onClick={() => sendUiAction("craft-recipe", { primary: recipe.id })}>
                      <img src={RECIPE_ICONS[recipe.icon]} alt="" aria-hidden="true" />
                      <span className="hud-recipe-name">{recipe.name}</span>
                      <span className="hud-recipe-meta">{Array.isArray(recipe.ingredients) ? recipe.ingredients.map((ingredient) => `${ingredient.count}x ${ingredient.name}`).join(" + ") : ""}</span>
                    </button>
                  );
                })}
              </div>
            </section>

            <section className="hud-inventory" aria-label="Inventory">
              <p className="section-title">Backpack</p>
              <div className="hud-inventory-grid">
                {mainSlots.map((slot) => (
                  <InventorySlot key={slot.slot} slot={slot} selectable={false} dragSlot={dragSlot} setDragSlot={setDragSlot} />
                ))}
              </div>
              <p className="section-title">Hotbar</p>
              <nav className="hud-hotbar-overlay" aria-label="Hotbar">
                {displayHotbar.map((slot) => (
                  <InventorySlot key={slot.slot} slot={slot} selectable dragSlot={dragSlot} setDragSlot={setDragSlot} />
                ))}
              </nav>
              <ControlHints inputMethod={inputMethod} craftingOpen />
            </section>
          </div>
        </section>
      ) : null}

      {!overlayOpen ? (
        <nav className="hud-hotbar" aria-label="Hotbar">
          {displayHotbar.map((slot) => (
            <InventorySlot key={slot.slot} slot={slot} selectable dragSlot={dragSlot} setDragSlot={setDragSlot} />
          ))}
        </nav>
      ) : null}
      {!overlayOpen ? <ControlHints inputMethod={inputMethod} craftingOpen={false} /> : null}
    </main>
  );
}

function App() {
  const [model] = useVoxelsBridgeModel({ route: Route.MainMenu, revision: 0, progress: 0, items: [] });
  const showAtmosphere = ![Route.MainMenu, Route.Hud, Route.Pause].includes(model.route);
  useDirectionalNavigation(model.route);

  // Example usage for future gameplay overlays:
  // const { runGuardedAction } = useVoxelsActionGate();
  // runGuardedAction("pause-resume", "resume", {});
  return (
    <>
      {showAtmosphere ? <MenuAtmosphere opacity={model.route === Route.Loading ? 0.9 : 1} /> : null}
      {(() => {
        switch (model.route) {
          case Route.Splash: return <SplashIntro model={model} />;
          case Route.MainMenu: return <MainMenu model={model} />;
          case Route.SaveSelection: return <WorldSelect model={model} />;
          case Route.WorldCreation: return <WorldCreation />;
          case Route.Loading: return <Loading model={model} />;
          case Route.Join: return <JoinGame />;
          case Route.Error: return <ErrorRoute model={model} />;
          case Route.FatalError: return <ErrorRoute model={model} />;
          case Route.Hud: return <HudRoute model={model} />;
          case Route.Pause: return <PauseMenu model={model} />;
          case Route.Settings: return <Settings model={model} />;
          case Route.ControlsCard: return <Controls />;
          default: return null;
        }
      })()}
    </>
  );
}

createRoot(document.getElementById("root")).render(<App />);
