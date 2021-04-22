import { PDTA } from './pdta.js';
import { readAB } from './aba.js';
export class SF2File {
    constructor(ab) {
        const r = readAB(ab);
        console.assert(r.read32String(), 'RIFF');
        let size = r.get32();
        console.assert(r.read32String(), 'sfbk');
        console.assert(r.read32String(), 'LIST');
        size -= 64;
        const sections = {};
        do {
            const sectionSize = r.get32();
            const section = r.read32String();
            size = size - sectionSize;
            if (section === 'pdta') {
                this.pdta = new PDTA(r);
            }
            else if (section === 'sdta') {
                console.assert(r.read32String(), 'smpl');
                const nsamples = (sectionSize - 4) / 2;
                const bit16s = new Int16Array(r.readN(sectionSize - 4).buffer);
                const s16tof32 = (i16) => i16 / 0x7fff;
                const data = new Float32Array(nsamples);
                for (let i = 0; i < nsamples; i++) {
                    data[i] = bit16s[i];
                }
                this.sdta = {
                    nsamples,
                    data,
                    bit16s,
                };
            }
            else {
                r.skip(sectionSize);
            }
        } while (size > 0);
    }
    static async fromURL(url = 'http://grepawk.com/ssr-bach/file.sf2') {
        return new SF2File(new Uint8Array(await (await fetch(url)).arrayBuffer()));
    }
}
