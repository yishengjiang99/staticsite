import { SF2File } from './sffile.js';
import { Shdr } from './sf.types.js';
export class SampleData {
  shdr: Shdr;
  sampleData: Float32Array;
  loop: number[];
  constructor(shdr: Shdr, sffile: SF2File) {
    this.shdr = shdr;
    const { start, end } = shdr;
    this.sampleData = sffile.sdta.data.subarray(start, end);
    this.loop = [
      this.shdr.startLoop - shdr.start,
      this.shdr.endLoop - shdr.start,
    ];
  }
  get AudioBuffer() {
    return new AudioBuffer({
      numberOfChannels: 1,
      sampleRate: this.shdr.sampleRate,
      length: 2,
    });
  }
}
