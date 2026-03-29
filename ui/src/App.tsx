import React, { useEffect, useMemo, useState } from "react";

type MeterKey = "poison" | "bleed" | "rot" | "frostbite" | "sleep" | "madness";

type MetersState = Record<MeterKey, number>;

const ZERO_METERS: MetersState = {
  poison: 0,
  bleed: 0,
  rot: 0,
  frostbite: 0,
  sleep: 0,
  madness: 0,
};

const BANNER_LABELS: Record<string, string> = {
  bleed: "Blood Loss",
  poison: "Poison",
  rot: "Rot",
  frostbite: "Frostbite",
  sleep: "Sleep",
  madness: "Madness",
};

const METER_ROWS: { key: MeterKey; rowId: string; fillId: string; icon: string }[] = [
  { key: "poison", rowId: "poisonRow", fillId: "poisonFill", icon: "poison.png" },
  { key: "bleed", rowId: "bleedRow", fillId: "bleedFill", icon: "blood_loss.png" },
  { key: "rot", rowId: "rotRow", fillId: "rotFill", icon: "rot.png" },
  { key: "frostbite", rowId: "frostbiteRow", fillId: "frostbiteFill", icon: "frostbite.png" },
  { key: "sleep", rowId: "sleepRow", fillId: "sleepFill", icon: "sleep.png" },
  { key: "madness", rowId: "madnessRow", fillId: "madnessFill", icon: "madness.png" },
];

function clamp01(x: number): number {
  return Math.max(0, Math.min(1, x));
}

/** Treat near-zero as empty so float/string noise does not leave rows visible. */
function meterActive(v: number): boolean {
  return Number.isFinite(v) && v > 0.01;
}

declare global {
  interface Window {
    setPoisonMeter?: (arg: string) => void;
    setBleedMeter?: (arg: string) => void;
    setRotMeter?: (arg: string) => void;
    setFrostbiteMeter?: (arg: string) => void;
    setSleepMeter?: (arg: string) => void;
    setMadnessMeter?: (arg: string) => void;
    showStatusProcBanner?: (kindRaw: string) => void;
    clearProcBanner?: () => void;
  }
}

export function App() {
  const [meters, setMeters] = useState<MetersState>(ZERO_METERS);
  const [bannerKind, setBannerKind] = useState<string | null>(null);
  const [bannerVisible, setBannerVisible] = useState(false);

  const assetBase = import.meta.env.BASE_URL;

  useEffect(() => {
    const applyOne = (key: MeterKey) => (arg: string) => {
      const v = Number(arg);
      setMeters((prev) => ({
        ...prev,
        [key]: Number.isNaN(v) ? 0 : v,
      }));
    };

    const hideAllMeters = () => setMeters({ ...ZERO_METERS });

    window.setPoisonMeter = applyOne("poison");
    window.setBleedMeter = applyOne("bleed");
    window.setRotMeter = applyOne("rot");
    window.setFrostbiteMeter = applyOne("frostbite");
    window.setSleepMeter = applyOne("sleep");
    window.setMadnessMeter = applyOne("madness");

    window.showStatusProcBanner = (kindRaw: string) => {
      hideAllMeters();
      const kind = String(kindRaw || "").trim().toLowerCase();
      if (!BANNER_LABELS[kind]) {
        return;
      }
      setBannerKind(kind);
      setBannerVisible(false);
      requestAnimationFrame(() => setBannerVisible(true));
    };

    window.clearProcBanner = () => {
      setBannerVisible(false);
      setBannerKind(null);
    };

    return () => {
      delete window.setPoisonMeter;
      delete window.setBleedMeter;
      delete window.setRotMeter;
      delete window.setFrostbiteMeter;
      delete window.setSleepMeter;
      delete window.setMadnessMeter;
      delete window.showStatusProcBanner;
      delete window.clearProcBanner;
    };
  }, []);

  const anyMeterVisible = useMemo(
    () => METER_ROWS.some((r) => meterActive(meters[r.key])),
    [meters],
  );

  const bannerLabel = bannerKind ? BANNER_LABELS[bannerKind] ?? "" : "";

  return (
    <>
      <div
        id="proc-banner-root"
        className={bannerVisible && bannerKind ? "is-visible" : ""}
        aria-hidden={bannerVisible && bannerKind ? "false" : "true"}
      >
        <div
          id="procBannerBand"
          className={`proc-banner-band${bannerKind ? ` proc-banner-band--${bannerKind}` : " proc-banner-band--bleed"}`}
        >
          <div className="proc-banner-band-inner">
            <span className="proc-banner-text" id="procBannerText">
              {bannerLabel}
            </span>
          </div>
        </div>
      </div>

      <div id="meter-panel" className={anyMeterVisible ? "" : "is-empty"}>
        {METER_ROWS.map((r) => {
          const v = meters[r.key];
          const active = meterActive(v);
          const raw = Number.isFinite(v) ? v : 0;
          const t = clamp01(raw / 100);
          const widthPct = (t * 100).toFixed(1);
          return (
            <div
              key={r.key}
              id={r.rowId}
              className={`meter-row${active ? "" : " is-hidden"}`}
            >
              <div className="meter-icon" aria-hidden="true">
                <img src={`${assetBase}assets/${r.icon}`} alt="" />
              </div>
              <div className="bar-track">
                <div
                  className="bar-fill"
                  id={r.fillId}
                  style={{
                    width: `${widthPct}%`,
                    opacity: active ? 1 : 0.35,
                  }}
                />
              </div>
            </div>
          );
        })}
      </div>
    </>
  );
}
