import { PDTA } from './pdta.js';
export declare class SF2File {
    pdta: PDTA;
    sdta: {
        nsamples: number;
        data: Float32Array;
        bit16s: Int16Array;
    };
    constructor(ab: Uint8Array);
    static fromURL(url?: string): Promise<SF2File>;
}
