export class SampleData {
    constructor(shdr, sffile) {
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
