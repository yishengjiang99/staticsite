import { readAB } from "./aba.js";
import { PDTA } from "./pdta.js";
import { SFZone } from "./Zone.js";

export class SF2File {
	pdta!: PDTA;
	sdta!: {
		nsamples: number;
		data: Uint8Array;
		floatArr: SharedArrayBuffer;
		iterator: any;
	};
	static fromURL: (url: string) => Promise<SF2File>;
	constructor(ab: ArrayBuffer | Uint8Array) {
		const r = readAB(ab);
		console.assert(r.read32String() == "RIFF");
		let size: number = r.get32();
		console.assert(r.read32String() == "sfbk");
		console.assert(r.read32String() == "LIST");
		size -= 64;
		const sections: any = {};
		do {
			const sectionSize = r.get32();
			const section = r.read32String();
			size = size - sectionSize;
			if (section === "pdta") {
				this.pdta = new PDTA(r);
			} else if (section === "sdta") {
				console.assert(r.read32String() == "smpl");
				const nsamples = (sectionSize - 4) / 2;
				const uint8s = r.readN(sectionSize - 4);
				const floatArr = new SharedArrayBuffer(uint8s.byteLength * 2);
				const dv2 = new DataView(floatArr);
				const dv = new DataView(uint8s.buffer);
				for (let i = 0; i < dv.byteLength / 2 - 1; i++) {
					dv2.setFloat32(i * 4, dv.getInt16(2 * i, true) / 0x7fff, true);
				}
				function iterate(zone: SFZone, key: number, outputSampleRate: number, length = 48000 * 2) {
					const data = new Float32Array(floatArr);

					const { start, end, startLoop, endLoop } = zone.sample;
					const loop = [startLoop - start, endLoop - start];
					const pitchRatio =
						(Math.pow(2, (key * 100 - zone.pitch) / 1200) * zone.sample.sampleRate) /
						outputSampleRate;
					function* shift() {
						let pos = 0x00;
						let n = 0;
						let shift = 0.0;
						while (n++ < length) {
							yield data[pos];
							shift = shift + pitchRatio;
							while (shift >= 1) {
								shift--;
								pos++;
							}
							if (pos >= loop[1]) pos = loop[0];
						}
						return data[pos];
					}
					return shift();
				}
				this.sdta = {
					nsamples,
					data: uint8s,
					floatArr: floatArr,
					iterator: iterate,
				};
			} else {
				r.skip(sectionSize);
			}
		} while (size > 0);
	}
}

SF2File.fromURL = async (url: string) => {
	return new SF2File(await (await fetch(url)).arrayBuffer());
};
