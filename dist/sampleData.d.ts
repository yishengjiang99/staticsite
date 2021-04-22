import { SF2File } from './sffile.js';
import { Shdr } from './sf.types.js';
export declare class SampleData {
    shdr: Shdr;
    sampleData: Float32Array;
    loop: number[];
    constructor(shdr: Shdr, sffile: SF2File);
    get AudioBuffer(): AudioBuffer;
}
