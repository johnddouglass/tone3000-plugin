import React, { useEffect, useRef, useState } from 'react';
import { KnobControl } from './KnobControl';
import { linearScale } from './knobScale';
import type { ParametricKnob } from '../types/chain';

interface ParametricKnobsProps {
  blockId: string;
  knobs: ParametricKnob[];
  /** Push the full updated value array to native (model's declared order). */
  onChange: (blockId: string, values: number[]) => void;
  knobSize?: number;
}

/**
 * [parametric] Renders the controls a parametric (.param.nam) model declares.
 * A continuous param (steps 0/1) is a KnobControl; a stepped param (steps >= 2,
 * e.g. a 3-way voicing switch) is a segmented button row. Every change pushes
 * the *complete* value array back to native via setBlockParametricKnobs — that
 * whole vector is what reaches the model's FiLM condition.
 *
 * Placement/visual styling here is intentionally minimal; it drops into the NAM
 * block's detail card and is meant to be refined against the live faceplate.
 */
export const ParametricKnobs: React.FC<ParametricKnobsProps> = ({
  blockId,
  knobs,
  onChange,
  knobSize,
}) => {
  const [values, setValues] = useState<number[]>(() => knobs.map((k) => k.value));
  const draggingRef = useRef(false);

  // Re-sync from native unless the user is mid-drag (a stale poll must not
  // fight the pointer). Keyed on the serialized values so external changes land.
  const incoming = knobs.map((k) => k.value).join(',');
  useEffect(() => {
    if (!draggingRef.current) setValues(knobs.map((k) => k.value));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [incoming, knobs.length]);

  if (knobs.length === 0) return null;

  const commit = (index: number, value: number) => {
    setValues((prev) => {
      const next = prev.slice();
      next[index] = value;
      onChange(blockId, next);
      return next;
    });
  };

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        flexWrap: 'wrap',
        gap: '16rem',
        alignItems: 'flex-end',
        justifyContent: 'center',
      }}
    >
      {knobs.map((knob, i) => {
        const value = values[i] ?? knob.value;

        if (knob.steps >= 2) {
          const span = knob.max - knob.min || 1;
          const positions = Array.from(
            { length: knob.steps },
            (_, s) => knob.min + (s * span) / (knob.steps - 1)
          );
          const activeIndex = Math.round(((value - knob.min) / span) * (knob.steps - 1));
          return (
            <div
              key={knob.name}
              style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '6rem' }}
            >
              <div style={{ display: 'flex', flexDirection: 'row', gap: '2rem' }}>
                {positions.map((pos, s) => (
                  <button
                    key={s}
                    type="button"
                    onClick={() => commit(i, pos)}
                    style={{
                      minWidth: '24rem',
                      padding: '4rem 8rem',
                      fontSize: '11rem',
                      cursor: 'pointer',
                      border: '1rem solid rgba(255,255,255,0.25)',
                      borderRadius: '4rem',
                      background: s === activeIndex ? 'rgba(255,255,255,0.85)' : 'transparent',
                      color: s === activeIndex ? '#111' : 'rgba(255,255,255,0.7)',
                    }}
                  >
                    {s + 1}
                  </button>
                ))}
              </div>
              <span style={{ fontSize: '12rem', color: 'rgba(255,255,255,0.7)' }}>{knob.name}</span>
            </div>
          );
        }

        return (
          <KnobControl
            key={knob.name}
            label={knob.name}
            value={value}
            min={knob.min}
            max={knob.max}
            defaultValue={knob.default}
            scale={linearScale(knob.min, knob.max, '', 2)}
            size={knobSize}
            thumb="secondary"
            onChange={(v) => commit(i, v)}
            onDragStateChange={(d) => {
              draggingRef.current = d;
            }}
          />
        );
      })}
    </div>
  );
};
