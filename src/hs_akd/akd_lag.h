#ifndef AKD_LAG_H
#define AKD_LAG_H


//the only thing that should go here is your interface definition!
//the rest does not matter to other modules (and if you want it to matter to other modules, then it belongs in the interface.)

//exception: any special structs that are being passed/returned to/by interface functions

//interface name..increment it whenever you change the interface layout..or bad things will happen to small animals.
#define I_AKD_LAG "akd_lag-2"

#define AKD_MAX_SAMPLESIZE 25
#define AKD_PING_SAMPLESIZE 25
#define AKD_ETC_SAMPLESIZE 25
#define AKD_DRIFT_SAMPLESIZE 10
#define AKD_PLOSS_SAMPLESIZE 16

#define AKD_LAG_MAXPING 1000

typedef enum akd_lag_broadtype
{
	AKD_PING_TYPE,
	AKD_LOSS_TYPE,
	AKD_ETC_TYPE
} akd_lag_broadtype;

typedef enum akd_lag_type
{
	AKD_S2CPING,
	AKD_C2SPING,
	AKD_RELPING,
	AKD_FLUX,
	AKD_FLUXB,
	AKD_DRIFT,
	AKD_S2CLOSS,
	AKD_C2SLOSS,
	AKD_WPNLOSS,
	AKD_TESTPING
} akd_lag_type;

#define IS_PING_TYPE(s) (((s)==AKD_S2CPING) || ((s)==AKD_C2SPING) || ((s)==AKD_RELPING))
#define IS_LOSS_TYPE(s) (((s)==AKD_S2CLOSS) || ((s)==AKD_C2SLOSS) || ((s)==AKD_WPNLOSS))
#define IS_ETC_TYPE(s) (((s)==AKD_FLUX) || ((s)==AKD_FLUXB) || ((s)==AKD_DRIFT))

//template for pseudo-object orientation so we can cast any type of data to this type
//note that there is no way to tell how many samples there are in all after casting, so you can only go off of d->taken
typedef struct akd_lag_broadsubdata
{
	akd_lag_broadtype broadtype;
	akd_lag_type type;
	i16 high;
	i16 sinceHigh;

	i8 taken;
	i8 current;

	i16 samples[0];
} akd_lag_broadsubdata;

typedef struct akd_lag_pingsubdata
{
	akd_lag_broadtype broadtype;
	akd_lag_type type;
	i16 high;
	i16 sinceHigh;

	i8 taken;
	i8 current;

	i16 samples[AKD_PING_SAMPLESIZE];

	i16 low;
} akd_lag_pingsubdata;

typedef struct akd_lag_etcsubdata
{
	akd_lag_broadtype broadtype;
	akd_lag_type type;
	i16 NAhigh;
	i16 NAsinceHigh;

	i8 taken;
	i8 current;

	i16 samples[AKD_ETC_SAMPLESIZE];

	i16 high;
	i16 sinceHigh;
} akd_lag_etcsubdata;

typedef struct akd_lag_lossubdata
{
	akd_lag_broadtype broadtype;
	akd_lag_type type;
	i16 NAhigh;
	i16 NAsinceHigh;

	i8 taken;
	i8 current;

	i16 samples[AKD_PLOSS_SAMPLESIZE];
	i16 weights[AKD_PLOSS_SAMPLESIZE];

	i16 high;
	i16 sinceHigh;
} akd_lag_losssubdata;

typedef struct akd_lag_data
{
	akd_lag_pingsubdata s2cping;
	akd_lag_pingsubdata c2sping;
	akd_lag_pingsubdata relping;
	akd_lag_etcsubdata flux;
	akd_lag_etcsubdata fluxB;
	akd_lag_etcsubdata drift;
	akd_lag_losssubdata s2closs;
	akd_lag_losssubdata c2sloss;
	akd_lag_losssubdata wpnloss;
	akd_lag_pingsubdata testping;
} akd_lag_data;

#define CB_LAGOUT "akd-lagout"
typedef void (*LagoutFunc)(Player *p, int oldfreq, const char *message);

typedef struct akd_lag_report
{
	short s2c_ping_low;
	short s2c_ping_high;
	short s2c_ping_ave;
	short c2s_ping_low;
	short c2s_ping_high;
	short c2s_ping_ave;
	short rel_ping_low;
	short rel_ping_high;
	short rel_ping_ave;
	short flux_high;
	short flux_ave;
	short fluxb_high;
	short fluxb_ave;
	short drift_high;
	short drift_ave;
	float s2c_loss_high;
	float s2c_loss_ave;
	float c2s_loss_high;
	float c2s_loss_ave;
} akd_lag_report;

typedef struct Iakd_lag
{
	INTERFACE_HEAD_DECL
	akd_lag_report *(*lagReport)(Player *, akd_lag_report *);
} Iakd_lag;

#endif
