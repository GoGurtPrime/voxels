import { useCallback, useEffect, useMemo, useRef, useState } from "react";

/**
 * Shared route ids published by the native UI model.
 * Keep these values aligned with PlayerUIRoute in C++.
 */
export const Route = {
  MainMenu: 0,
  SaveSelection: 1,
  WorldCreation: 2,
  Loading: 3,
  Join: 4,
  Error: 5,
  Pause: 6,
  Settings: 7,
  ControlsCard: 8,
  Hud: 9,
  FatalError: 10
};

/**
 * Bridge envelope version expected by both C++ and browser code.
 * Any schema change must be reflected in docs/PLAYER_UI_BRIDGE_API.md.
 */
export const BridgeProtocolVersion = 1;
export const BridgeModelKind = "ui.model";
const BridgeActionPrefix = "ui.action.";

let nextRequestId = 1;

/**
 * Defensive parser for unknown values arriving from the bridge.
 * Returns fallback instead of throwing so UI routes keep rendering.
 */
export function parseJson(value, fallback) {
  if (value == null) return fallback;
  if (typeof value === "object") return value;
  if (typeof value !== "string") return fallback;
  try {
    return JSON.parse(value);
  } catch {
    return fallback;
  }
}

/**
 * Returns a validated bridge envelope object or null when invalid.
 */
export function parseBridgeEnvelope(candidate) {
  const envelope = parseJson(candidate, null);
  if (!envelope || typeof envelope !== "object") return null;
  if (envelope.version !== BridgeProtocolVersion) return null;
  if (typeof envelope.kind !== "string") return null;
  if (typeof envelope.requestId !== "number" || envelope.requestId <= 0) return null;
  if (typeof envelope.payload !== "string") return null;
  return envelope;
}

/**
 * Encode and send one UI action to native.
 * Supports both names so old and new CEF bindings both work.
 */
export function sendUiAction(kind, fields = {}) {
  const envelope = {
    version: BridgeProtocolVersion,
    kind: `${BridgeActionPrefix}${kind}`,
    requestId: nextRequestId++,
    payload: JSON.stringify(fields)
  };

  const encoded = JSON.stringify(envelope);
  if (typeof window.voxelsBridgeSend === "function") {
    window.voxelsBridgeSend(encoded);
    return envelope.requestId;
  }
  if (typeof window.voxelsAction === "function") {
    window.voxelsAction(encoded);
    return envelope.requestId;
  }
  return 0;
}

/**
 * Hook: tracks the latest authoritative model coming from native.
 * It subscribes to both the versioned bridge endpoint and the
 * legacy plain-model endpoint for compatibility.
 */
export function useVoxelsBridgeModel(initialModel) {
  const [model, setModel] = useState(initialModel);

  useEffect(() => {
    const receiveModel = (candidate) => {
      const parsed = parseJson(candidate, null);
      if (!parsed || typeof parsed !== "object") return;
      setModel((previous) => ({ ...previous, ...parsed }));
    };

    const receiveBridge = (candidate) => {
      const envelope = parseBridgeEnvelope(candidate);
      if (!envelope || envelope.kind !== BridgeModelKind) return;
      const payload = parseJson(envelope.payload, null);
      if (!payload || typeof payload !== "object") return;
      setModel((previous) => ({ ...previous, ...payload }));
    };

    window.__voxelsReceiveModel = receiveModel;
    window.__voxelsReceiveBridgeMessage = receiveBridge;

    if (window.__voxelsLastBridgeMessage) {
      receiveBridge(window.__voxelsLastBridgeMessage);
    } else if (window.__voxelsLastModel) {
      receiveModel(window.__voxelsLastModel);
    }

    return () => {
      delete window.__voxelsReceiveModel;
      delete window.__voxelsReceiveBridgeMessage;
    };
  }, []);

  return [model, setModel];
}

/**
 * Hook: stable action sender callback for React components.
 */
export function useVoxelsActionSender() {
  return useCallback((kind, fields = {}) => sendUiAction(kind, fields), []);
}

/**
 * Hook: prevents accidental duplicate submissions for gameplay-affecting
 * actions (create world, join, apply settings, etc).
 *
 * This is a lightweight cooldown gate, not an authoritative ack protocol.
 * Native C++ still decides whether an action is accepted.
 */
export function useVoxelsActionGate(defaultCooldownMs = 350) {
  const [pending, setPending] = useState({});
  const timersRef = useRef(new Map());

  useEffect(() => {
    return () => {
      for (const timeoutId of timersRef.current.values()) {
        clearTimeout(timeoutId);
      }
      timersRef.current.clear();
    };
  }, []);

  const clearPending = useCallback((key) => {
    setPending((current) => {
      if (!current[key]) return current;
      const next = { ...current };
      delete next[key];
      return next;
    });
    const timeoutId = timersRef.current.get(key);
    if (timeoutId) {
      clearTimeout(timeoutId);
      timersRef.current.delete(key);
    }
  }, []);

  const runGuardedAction = useCallback((key, kind, fields = {}, cooldownMs = defaultCooldownMs) => {
    if (!key) return 0;
    if (pending[key]) return 0;

    const requestId = sendUiAction(kind, fields);
    if (requestId <= 0) return 0;

    setPending((current) => ({ ...current, [key]: true }));

    const timeoutId = setTimeout(() => {
      setPending((current) => {
        if (!current[key]) return current;
        const next = { ...current };
        delete next[key];
        return next;
      });
      timersRef.current.delete(key);
    }, Math.max(80, cooldownMs));

    timersRef.current.set(key, timeoutId);
    return requestId;
  }, [defaultCooldownMs, pending]);

  const isPending = useCallback((key) => Boolean(pending[key]), [pending]);

  return useMemo(() => ({ runGuardedAction, isPending, clearPending, pending }), [clearPending, isPending, pending, runGuardedAction]);
}
