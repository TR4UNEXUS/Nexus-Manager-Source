// Restore the user's boot configuration after the temporary installer autoboot.
#include "boot_config.h"
#include "../../shared/hekate_icons.h"
#include "../../shared/nexus_boot_profile.h"
#include <libs/fatfs/ff.h>
#include <mem/heap.h>
#define BOOT_INI "sd:/bootloader/hekate_ipl.ini"
#define BOOT_BACKUP "sd:/config/nexus-hats-installer/hekate_ipl.ini.bak"
#define LEGACY_BACKUP "sd:/config/nexus-hats-installer/legacy-hekate_ipl.ini.bak"
static int read_ini(const char *path, char **data, size_t *size) {
    FIL file; int rc=f_open(&file,path,FA_READ); if (rc!=FR_OK) return rc;
    u64 length=f_size(&file);
    if (!length || length>NEXUS_INI_LIMIT) { f_close(&file); return FR_INVALID_PARAMETER; }
    *data=malloc((size_t)length);
    if (!*data) { f_close(&file); return FR_NOT_ENOUGH_CORE; }
    UINT read=0; rc=f_read(&file,*data,(UINT)length,&read); int close_rc=f_close(&file);
    if (rc==FR_OK && read!=length) rc=FR_DISK_ERR;
    if (rc==FR_OK) rc=close_rc;
    if (rc==FR_OK && memchr(*data,0,(size_t)length)) rc=FR_INVALID_PARAMETER;
    if (rc!=FR_OK) { free(*data); *data=NULL; return rc; }
    *size=(size_t)length; return FR_OK;
}
static int image_exists(const char *path) {
    FIL file; if (f_open(&file,path,FA_READ)!=FR_OK) return 0;
    unsigned char header[30]; UINT read=0;
    int ok=f_read(&file,header,sizeof(header),&read)==FR_OK && read==sizeof(header) &&
           header[0]=='B' && header[1]=='M' && f_size(&file)>sizeof(header);
    if (f_close(&file)!=FR_OK) ok=0;
    return ok;
}
int restore_hekate_config(bool installed, unsigned *icons_changed) {
    char *original=NULL,*pack=NULL,*merged=NULL; size_t size=0,pack_size=0;
    *icons_changed=0;
    int rc=read_ini(BOOT_BACKUP,&original,&size);
    if (rc==FR_NO_FILE || rc==FR_NO_PATH) rc=read_ini(LEGACY_BACKUP,&original,&size);
    if (rc!=FR_OK) return rc;
    char *output=original; size_t output_size=size;
    if (installed) {
        rc=read_ini(BOOT_INI,&pack,&pack_size);
        if (rc!=FR_OK || !nexus_boot_mode(pack,pack_size)) {
            free(original);free(pack);return rc==FR_OK?FR_INVALID_PARAMETER:rc;
        }
    }
    // Only a completed install changes dependencies. A failed/cancelled install
    // restores the original bytes; the recovery copy is kept until readback.
    if (installed) {
        if (nexus_themed_pack(pack,pack_size) &&
            (!image_exists("sd:/" NEXUS_EMU_ICON) || !image_exists("sd:/" NEXUS_SYS_ICON))) {
            free(original);free(pack);return FR_INVALID_PARAMETER;
        }
        size_t capacity=size*3+16384;
        merged=malloc(capacity);
        if (!merged) { free(original); free(pack); return FR_NOT_ENOUGH_CORE; }
        if (!nexus_merge_boot_profile(original,size,pack,pack_size,merged,capacity,&output_size,icons_changed)) {
            free(merged); free(original); free(pack); return FR_INVALID_PARAMETER;
        }
        output=merged;
    }
    free(pack);
    f_mkdir("sd:/bootloader");
    FIL file; rc=f_open(&file,BOOT_INI,FA_CREATE_ALWAYS|FA_WRITE);
    if (rc==FR_OK) {
        UINT written=0; rc=f_write(&file,output,(UINT)output_size,&written);
        if (rc==FR_OK && written!=output_size) rc=FR_DISK_ERR;
        if (rc==FR_OK) rc=f_sync(&file);
        int close_rc=f_close(&file); if (rc==FR_OK) rc=close_rc;
    }
    // Verify the restored bytes before removing the recovery copy.
    char *verify=NULL; size_t verify_size=0;
    if (rc==FR_OK) {
        rc=read_ini(BOOT_INI,&verify,&verify_size);
        if (rc==FR_OK && (verify_size!=output_size || memcmp(verify,output,output_size))) rc=FR_DISK_ERR;
    }
    free(verify); free(merged); free(original);
    if (rc==FR_OK) { f_unlink(BOOT_BACKUP); f_unlink(LEGACY_BACKUP); }
    return rc;
}
