
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "read2.h"
#include <math.h>
#include <unistd.h>
#include <time.h>
#define sr 48000
static float outputLeft[128];
static float outputRight[128];
static float outputInterweaved[128 * 2];

typedef struct _channel
{
	voice *voice;
	int program_number;
	float midi_gain_cb;
	float midi_pan;
} channel_t;
typedef struct
{
	int sampleRate;
	int currentFrame_start;
	int samples_per_frame;
	channel_t *channels;
} ctx_t;
static float centdbLUT[960];
static float centdblut(int x)
{
	if (x < 0)
		x = 0;
	if (x > 960)
		x = 960;

	return centdbLUT[x];
}
int nvoices();
static ctx_t *ctx;
static voice *vptr;
int readsf(FILE *fd)
{
	header_t *header = (header_t *)malloc(sizeof(header_t));
	header2_t *h2 = (header2_t *)malloc(sizeof(header2_t));
	fread(header, sizeof(header_t), 1, fd);

	printf("%.4s %.4s %.4s %u\n", header->name, header->sfbk, header->list, header->size);
	fread(h2, sizeof(header2_t), 1, fd);
	printf("\n%.4s %u", h2->name, h2->size);
	info = malloc(h2->size);
	fread(info, h2->size, 1, fd);
	fread(h2, sizeof(header2_t), 1, fd);
	printf("\n%.4s %u", h2->name, h2->size);
	data = (short *)malloc(h2->size * sizeof(short));
	sdta = (float *)malloc(h2->size * sizeof(float));
	float *trace = sdta;
	nsamples = h2->size / sizeof(short);

	printf("\n\t %ld", ftell(fd));
	fread(data, sizeof(short), nsamples, fd);

	for (int i = 0; i < nsamples; i++)
	{
		*trace++ = *(data + i) / 32767.0f;
	}
	//free(data);

#define readSection(section)                  \
	fread(sh, sizeof(section_header), 1, fd);   \
	printf("%.4s:%u\n", sh->name, sh->size);    \
	n##section##s = sh->size / sizeof(section); \
	section##s = (section *)malloc(sh->size);   \
	fread(section##s, sizeof(section), sh->size / sizeof(section), fd);

	section_header *sh = (section_header *)malloc(sizeof(section_header));

	fread(h2, sizeof(header2_t), 1, fd);

	readSection(phdr);
	readSection(pbag);
	readSection(pmod);
	readSection(pgen);
	readSection(inst);
	readSection(ibag);
	readSection(imod);
	readSection(igen);
	readSection(shdr);
	return 1;
}

static float p2over1200LUT[1200];
static inline float p2over1200(int x)
{
	if (x < -12000)
		return 0;
	if (x < 0)
		return 1.f / p2over1200(-x);
	else if (x > 1200.0f)
	{
		return 2 * p2over1200(x - 1200.0f);
	}
	else
	{
		return p2over1200LUT[(unsigned short)(x)];
	}
}

adsr_t *newEnvelope(short centAtt, short centRelease, short centDecay, short sustain, int sampleRate)
{
	adsr_t *env = (adsr_t *)malloc(sizeof(adsr_t));

	env->att_steps = fmax(p2over1200(centAtt) * sampleRate, 12);
	env->decay_steps = fmax(p2over1200(centDecay) * sampleRate, 12);
	env->release_steps = fmax(p2over1200(centRelease) * sampleRate, 12);

	env->att_rate = -960.0f / env->att_steps;
	env->decay_rate = -powf(10.0f, sustain / -200.0f) * powf(2.f, centDecay / 1200.0f);
	env->release_rate = 960.0f / env->release_steps;
	env->db_attenuate = 960.0f;

	return env;
}
float envShift(adsr_t *env)
{
	if (env->att_steps > 0)
	{
		env->att_steps--;
		env->db_attenuate += env->att_rate;
	}
	else if (env->decay_steps > 0)
	{
		env->decay_steps--;
		float egval = (1 - env->db_attenuate) * 960.0f;
		egval *= env->decay_rate;
		env->db_attenuate = 960.0f * egval - egval;
	}
	else if (env->release_steps > 0)
	{
		env->release_steps--;
		env->db_attenuate += env->release_rate;
	}
	if (env->db_attenuate > 960)
	{
		env->db_attenuate = 960.0f;
	}
	if (env->db_attenuate < 0.0)
	{
		env->db_attenuate = 0.0f;
	}

	return env->db_attenuate;
}
void adsrRelease(adsr_t *env)
{
	env->decay_steps = 0;
	env->att_steps = 0;
}

voice *newVoice(zone_t *z, int key, int vel)
{

	voice *v = (voice *)malloc(sizeof(voice));
	v->ampvol = newEnvelope(z->VolEnvAttack, z->VolEnvRelease, z->VolEnvDecay, z->VolEnvSustain, 48000);
	short rt = z->OverrideRootKey > -1 ? z->sample->originalPitch : z->sample->originalPitch;
	float sampleTone = rt * 100.0f + z->CoarseTune * 100.0f + (float)z->FineTune;
	float octaveDivv = ((float)key * 100 - sampleTone) / 1200.0f;
	v->ratio = 1.0f * pow(2.0f, octaveDivv) * z->sample->sampleRate / 48000;

	v->pos = z->start;
	v->frac = 0.0f;
	v->z = z;
	v->midi = key;
	v->velocity = vel;
	v->next = NULL;

	return v;
}

void index22(int pid, int bkid, int key, int vel, int chid)
{
	short igset[60] = {0};
	int instID = -1;
	int lastSampId = -1;
	short pgdef[60] = {0};
	for (int i = 0; i < nphdrs - 1; i++)
	{
		if (phdrs[i].bankId != bkid || phdrs[i].pid != pid)
			continue;
		int lastbag = phdrs[i + 1].pbagNdx;

		for (int j = phdrs[i].pbagNdx; j < lastbag; j++)
		{
			pbag *pg = pbags + j;
			pgen_t *lastg = pgens + pg[j + 1].pgen_id;
			int pgenId = pg->pgen_id;
			int lastPgenId = j < npbags - 1 ? pbags[j + 1].pgen_id : npgens - 1;
			short pgset[60] = {0};
			instID = -1;
			for (int k = pgenId; k < lastPgenId; k++)
			{
				pgen_t *g = pgens + k;

				if (g->operator== 44 &&(g->val.ranges.lo > vel || g->val.ranges.hi < vel))
					break;
				if (g->operator== 43 &&(g->val.ranges.lo > key || g->val.ranges.hi < key))
					break;
				if (g->operator== 41)
				{
					instID = g->val.shAmount;
				}
				pgset[g->operator] = g->val.shAmount;
			}
			if (instID == -1)
			{
				memcpy(pgdef, pgset, sizeof(pgset));
			}
			else
			{
				inst *ihead = insts + instID;
				int ibgId = ihead->ibagNdx;
				int lastibg = (ihead + 1)->ibagNdx;
				lastSampId = -1;
				short igdef[60] = {0};
				for (int ibg = ibgId; ibg < lastibg; ibg++)
				{
					short igset[60];
					ibag *ibgg = ibags + ibg;
					pgen_t *lastig = ibg < nibags - 1 ? igens + (ibgg + 1)->igen_id : igens + nigens - 1;
					for (pgen_t *g = igens + ibgg->igen_id; g->operator!= 60 && g != lastig; g++)
					{
						if (g->operator== 44 &&(g->val.ranges.lo > vel || g->val.ranges.hi < vel))
							break;
						if (g->operator== 43 &&(g->val.ranges.lo > key || g->val.ranges.hi < key))
							break;

						igset[g->operator]=g->val.shAmount;
						if (g->operator== 53)
						{
							lastSampId = g->val.shAmount; // | (ig->val.ranges.hi << 8);
							short *attributes = (short *)malloc(sizeof(short) * 60);
							for (int i = 0; i < 60; i++)
							{
								if (igset[i])
									*(attributes + i) = igset[i];
								else if (igdef[i])
									*(attributes + i) = igdef[i];

								if (pgset[i])
									*(attributes + i) += pgset[i];
								else if (pgdef[i])
									*(attributes + i) += pgdef[i];
							}
							zone_t *z = (zone_t *)malloc(sizeof(zone_t));
							memcpy(z, attributes, 60 * sizeof(short));

							shdrcast *sh = (shdrcast *)(shdrs + lastSampId);

							z->sample = sh;

							z->start = sh->start + ((unsigned short)(z->StartAddrCoarseOfs & 0x7f) << 15) + (unsigned short)(z->StartAddrOfs & 0x7f);
							z->end = sh->end + ((unsigned short)(z->EndAddrCoarseOfs & 0x7f) << 15) + (unsigned short)(z->EndAddrOfs & 0x7f);
							z->endloop = sh->endloop + ((unsigned short)(z->EndLoopAddrCoarseOfs & 0x7f) << 15) + (unsigned short)(z->EndLoopAddrOfs & 0x7f);
							z->startloop = sh->startloop + (unsigned short)(z->StartLoopAddrCoarseOfs & 0x7f << 15) + (unsigned short)(z->StartLoopAddrOfs & 0x7f);

							voice *v = vptr;
							voice *nv = newVoice(z, key, vel);
							nv->chid = chid;
							int added = 0;
							while (v->next != NULL)
							{
								if (v->next->chid == chid && v->next->ampvol->db_attenuate > 920.0f)
								{
									nv->next = v->next->next;
									v->next = nv;
									break;
								}
								v = v->next;
							}
							if (!added)
								v->next = nv;
						}
					}
				}
			}
		}
	}
}
void initLUTs()
{
	for (int i = 0; i < 1200; i++)
	{
		p2over1200LUT[i] = powf(2.0f, i / 1200.0f);
	}
	for (int i = 0; i < 960; i++)
	{
		centdbLUT[i] = powf(10.0f, i / -200);
	}
	vptr = (voice *)malloc(sizeof(voice));
}

void loop(voice *v)
{
	uint32_t loopLength = v->z->endloop - v->z->startloop;
	int cb_attentuate = centdblut(v->z->Attenuation);
	int16_t pan = v->z->Pan;
	float panLeft = (0.5f - (float)pan / 1000.0f) * cb_attentuate;
	float panright = (0.5f + (float)pan / 1000.0f) * cb_attentuate;
	for (int i = 0; i < 128; i++)
	{
		float f1 = *(sdta + v->pos);
		float f2 = *(sdta + v->pos + 1);
		float gain = f1 + (f2 - f1) * v->frac;

		float mono = gain * centdblut(envShift(v->ampvol));

		outputInterweaved[2 * i] += mono * panLeft;
		outputInterweaved[2 * i + 1] += mono * panright;
		v->frac += v->ratio;
		while (v->frac >= 1.0f)
		{
			v->frac--;
			v->pos++;
		}
		if (v->pos >= v->z->sample->endloop)
		{
			v->pos -= loopLength;
		}
	}
}
void render(int frames, FILE *output)
{
	while (frames >= 0)
	{
		voice *t = vptr;
		bzero(outputInterweaved, sizeof(float) * ctx->samples_per_frame);

		while (t->next)
		{
			t = t->next;
			loop(t);
		}
		frames -= 128;

		fwrite(outputInterweaved, 2 * 128, 4, output);
	}
}
void noteOn(int channelNumber, int midi, int vel)
{
	int programNumber = (ctx->channels + channelNumber)->program_number;
	printf("\n notes n %d", nvoices());
	index22(programNumber & 0x7f, programNumber & 0x80, midi, vel, channelNumber);
	printf("\n notes n %d", nvoices());
}
void noteOff(int channelNumber, int midi)
{
	voice *v = vptr;
	while (v->next)
	{
		if (v->next->chid == channelNumber && v->next->midi == midi)
		{
			adsrRelease(v->next->ampvol);
			break;
		}
		v = v->next;
	}
}
void init_ctx()
{
	ctx = (ctx_t *)malloc(sizeof(ctx_t));
	ctx->sampleRate = 48000;
	ctx->currentFrame_start = 0;
	ctx->samples_per_frame = 127;
	ctx->channels = (channel_t *)malloc(sizeof(channel_t) * 16);
	for (int i = 0; i < 16; i++)
	{
		(ctx->channels + i)->program_number = 0;
		(ctx->channels + i)->midi_pan = 1.f;
		(ctx->channels + i)->midi_gain_cb = 89.0f;
		(ctx->channels + i)->voice = (voice *)malloc(sizeof(voice));
	}
	vptr = (voice *)malloc(sizeof(voice));
	vptr->next = NULL;
}
int mkfifo(char *sf);
int nvoices()
{
	int n = 0;
	voice *t = vptr;
	while (t != NULL && t->next != NULL)
	{
		t = t->next;
		printf("\n%d", t->ampvol->att_steps);
		n++;
	}
	return n;
}
#include "biquad.c"
int main()
{
	//	FILE *dl = popen("curl -s 'https://www.grepawk.com/static/Timpani-20201121.sf2' -o -", "r");
	// 	printf("\n%f, ", stbl[i]);
	init_ctx();
	printf("%p", vptr);

	initLUTs();
	FILE *dl = fopen("file.sf2", "rb");
	if (!dl)
	{
		dl = popen("curl -s 'https://grep32bit.blob.core.windows.net/sf2/Acoustic%20Guitar.sf2' -O", "r");
	}
	readsf(dl);
	//fclose(dl);
	for (int i = 0; i < nphdrs; i++)
		printf("\n %d %s %d", i, phdrs[i].name, phdrs[i].pid);

	init_ctx();
	ctx->channels[0].program_number = phdrs[0].pid;

	ctx->channels[1].program_number = phdrs[0].pid;
	FILE *pngo;
	//FILE *proc = popen("ffplay -ar 48000 -ac 2 -f f32le -i pipe:0", "w");

	biquad *b = BiQuad_new(LPF, 1, 10000, 48000, 1);
	printf("%f", BiQuad(0, b));
	noteOn(0, 44, 128);

	FILE *pngo = popen("ffmpeg -y -ar 48000 -ac 2 -f f32le -i pipe:0 -f f32le 44-128.pcm tee pipe:1 | ffplay -i pipe:0 -f f32le", "w");

	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	render(128, pngo);
	//	pngo = popen("ffmpeg -y -f f32le -i pipe:0 -filter_complex showwavespic=s=400x400 -frames:v 50 ff.png", "w");

	pclose(pngo);
	return 0;
}