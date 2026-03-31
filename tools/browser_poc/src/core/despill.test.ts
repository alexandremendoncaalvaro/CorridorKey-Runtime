import { describe, expect, it } from "vitest";
import { despill_foreground } from "./despill";

describe("despill_foreground", () => {
  it("reduces excess green and redistributes part of the spill to red and blue", () => {
    const foreground = new Float32Array([0.2, 0.8, 0.1]);

    const output = despill_foreground(foreground, 1, 1);

    expect(output[0]).toBeGreaterThan(foreground[0]);
    expect(output[1]).toBeLessThan(foreground[1]);
    expect(output[2]).toBeGreaterThan(foreground[2]);
  });

  it("leaves balanced pixels unchanged", () => {
    const foreground = new Float32Array([0.4, 0.3, 0.2]);

    const output = despill_foreground(foreground, 1, 1);

    expect(Array.from(output)).toEqual(Array.from(foreground));
  });
});
