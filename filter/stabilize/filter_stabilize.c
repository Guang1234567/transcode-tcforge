/*
 *  filter_stabilize.c
 *
 *  Copyright (C) Georg Martius - June 2007
 *   georg dot martius at web dot de  
 *
 *  This file is part of transcode, a video stream processing tool
 *      
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

/* Typical call:
 *  transcode -V -J stabilize=shakiness=5:show=1,preview 
 *         -i inp.mpeg -y null,null -o dummy
 *  all parameters are optional
*/

#define MOD_NAME    "filter_stabilize.so"
#define MOD_VERSION "v0.75 (2010-04-07)"
#define MOD_CAP     "extracts relative transformations of \n\
    subsequent frames (used for stabilization together with the\n\
    transform filter in a second pass)"
#define MOD_AUTHOR  "Georg Martius"

/* Ideas:
 - Try OpenCL/Cuda, this should work great
 - use smoothing on the frames and then use gradient decent!
 - stepsize could be adapted (maybe to check only one field with large
   stepsize and use the maximally required for the other fields
*/

#define MOD_FEATURES \
    TC_MODULE_FEATURE_FILTER|TC_MODULE_FEATURE_VIDEO
#define MOD_FLAGS  \
    TC_MODULE_FLAG_RECONFIGURABLE | TC_MODULE_FLAG_DELAY
  
#include "src/transcode.h"
#include "src/filter.h"
#include "libtc/libtc.h"
#include "libtc/tccodecs.h"
#include "libtcutil/optstr.h"
#include "libtcutil/tclist.h"
#include "libtcmodule/tcmodule-plugin.h"

#include "transform.h"

#include <math.h>
#include <libgen.h>

/* if defined we are very verbose and generate files to analyse
 * this is really just for debugging and development */
// #define STABVERBOSE

typedef struct _field {
    int x;     // middle position x
    int y;     // middle position y
    int size;  // size of field
} Field;

// structure that contains the contrast and the index of a field
typedef struct _contrast_idx {
    double contrast;
    int index;
} contrast_idx;

/* private date structure of this filter*/
typedef struct _stab_data {
    size_t framesize;  // size of frame buffer in bytes (prev)
    unsigned char* curr; // current frame buffer (only pointer)
    unsigned char* currcopy; // copy of the current frame needed for drawing
    unsigned char* prev; // frame buffer for last frame (copied)
    short hasSeenOneFrame; // true if we have a valid previous frame

    vob_t* vob;  // pointer to information structure
    int width, height;

    /* list of transforms*/
    TCList* transs;

    Field* fields;


    /* Options */
    /* maximum number of pixels we expect the shift of subsequent frames */
    int maxshift; 
    int stepsize; // stepsize of field transformation detection
    int allowmax; // 1 if maximal shift is allowed
    int algo;     // algorithm to use
    int field_num;  // number of measurement fields
    int maxfields;  // maximum number of fields used (selected by contrast)
    int field_size; // size    = min(sd->width, sd->height)/10;
    int field_rows; // number of rows
    /* if 1 and 2 then the fields and transforms are shown in the frames */
    int show; 
    /* measurement fields with lower contrast are discarded */
    double contrast_threshold;            
    /* maximal difference in angles of fields */
    double maxanglevariation;
    /* meta parameter for maxshift and fieldsize between 1 and 10 */
    int shakiness;   
    int accuracy;   // meta parameter for number of fields between 1 and 10
  
    int t;
    char* result;
    FILE* f;

    char conf_str[TC_BUF_MIN];
} StabData;

/* type for a function that calculates the transformation of a certain field 
 */
typedef Transform (*calcFieldTransFunc)(StabData*, const Field*, int);

/* type for a function that calculates the contrast of a certain field 
 */
typedef double (*contrastSubImgFunc)(StabData* sd, const Field* field);

static const char stabilize_help[] = ""
    "Overview:\n"
    "    Generates a file with relative transform information\n"
    "     (translation, rotation) about subsequent frames."
    " See also transform.\n" 
    "Options\n"
    "    'result'      path to the file used to write the transforms\n"
    "                  (def:inputfile.stab)\n"
    "    'shakiness'   how shaky is the video and how quick is the camera?\n"
    "                  1: little (fast) 10: very strong/quick (slow) (def: 4)\n"
    "    'accuracy'    accuracy of detection process (>=shakiness)\n"
    "                  1: low (fast) 15: high (slow) (def: 4)\n"
    "    'stepsize'    stepsize of search process, region around minimum \n"
    "                  is scanned with 1 pixel resolution (def: 6)\n"
    "    'algo'        0: brute force (translation only);\n"
    "                  1: small measurement fields (def)\n"
    "    'mincontrast' below this contrast a field is discarded (0-1) (def: 0.3)\n"
    "    'show'        0: draw nothing (def); 1,2: show fields and transforms\n"
    "                  in the resulting frames. Consider the 'preview' filter\n"
    "    'help'        print this help message\n";

int initFields(StabData* sd);
double compareImg(unsigned char* I1, unsigned char* I2, 
		  int width, int height,  int bytesPerPixel, int d_x, int d_y);
double compareSubImg(unsigned char* const I1, unsigned char* const I2, 
		     const Field* field, 
		     int width, int height, int bytesPerPixel,int d_x,int d_y);
double contrastSubImgYUV(StabData* sd, const Field* field);
double contrastSubImgRGB(StabData* sd, const Field* field);
double contrastSubImg(unsigned char* const I, const Field* field, 
                      int width, int height, int bytesPerPixel);
int cmp_contrast_idx(const void *ci1, const void* ci2);
TCList* selectfields(StabData* sd, contrastSubImgFunc contrastfunc);

Transform calcShiftRGBSimple(StabData* sd);
Transform calcShiftYUVSimple(StabData* sd);
double calcAngle(StabData* sd, Field* field, Transform* t,
                 int center_x, int center_y);
Transform calcFieldTransYUV(StabData* sd, const Field* field, 
                            int fieldnum);
Transform calcFieldTransRGB(StabData* sd, const Field* field, 
                            int fieldnum);
Transform calcTransFields(StabData* sd, calcFieldTransFunc fieldfunc,
                          contrastSubImgFunc contrastfunc);


void drawFieldScanArea(StabData* sd, const Field* field, const Transform* t);
void drawField(StabData* sd, const Field* field, const Transform* t);
void drawFieldTrans(StabData* sd, const Field* field, const Transform* t);
void drawBox(unsigned char* I, int width, int height, int bytesPerPixel, 
             int x, int y, int sizex, int sizey, unsigned char color);
void addTrans(StabData* sd, Transform sl);

void addTrans(StabData* sd, Transform sl)
{
    if (!sd->transs) {
        sd->transs = tc_list_new(0);
    }
    tc_list_append_dup(sd->transs, &sl, sizeof(sl));
}



/** initialise measurement fields on the frame.
    The size of the fields and the maxshift is used to 
    calculate an optimal distribution in the frame.
*/
int initFields(StabData* sd)
{
    int size     = sd->field_size;
    int rows = TC_MAX(3,(sd->height - sd->maxshift*2)/size-1);
    int cols = TC_MAX(3,(sd->width  - sd->maxshift*2)/size-1);
    // make sure that the remaining rows have the same length
    sd->field_num  = rows*cols;
    sd->field_rows = rows;
    // tc_log_msg(MOD_NAME, "field setup: rows: %i cols: %i Total: %i fields", 
    //            rows, cols, sd->field_num);

    if (!(sd->fields = tc_malloc(sizeof(Field) * sd->field_num))) {
        tc_log_error(MOD_NAME, "malloc failed!\n");
        return 0;
    } else {
        int i, j;
        // the border is the amount by which the field centers
        // have to be away from the image boundary
        // (stepsize is added in case shift is increased through stepsize)
        int border   = size/2 + sd->maxshift + sd->stepsize;
        int step_x   = (sd->width  - 2*border)/TC_MAX(cols-1,1);
        int step_y   = (sd->height - 2*border) / TC_MAX(rows-1,1);
        for (j = 0; j < rows; j++) {
            for (i = 0; i < cols; i++) {
                int idx = j*cols+i;
                sd->fields[idx].x = border + i*step_x;
                sd->fields[idx].y = border + j*step_y;
                sd->fields[idx].size = size;
            }
        }
    }
    return 1;
}


/**
   compares the two given images and returns the average absolute difference
   \param d_x shift in x direction
   \param d_y shift in y direction
*/
double compareImg(unsigned char* I1, unsigned char* I2, 
                  int width, int height,  int bytesPerPixel, int d_x, int d_y)
{
    int i, j;
    unsigned char* p1 = NULL;
    unsigned char* p2 = NULL;
    long int sum = 0;  
    int effectWidth = width - abs(d_x);
    int effectHeight = height - abs(d_y);

/*   DEBUGGING code to export single frames */
/*   char buffer[100]; */
/*   sprintf(buffer, "pic_%02ix%02i_1.ppm", d_x, d_y); */
/*   FILE *pic1 = fopen(buffer, "w"); */
/*   sprintf(buffer, "pic_%02ix%02i_2.ppm", d_x, d_y); */
/*   FILE *pic2 = fopen(buffer, "w"); */
/*   fprintf(pic1, "P6\n%i %i\n255\n", effectWidth, effectHeight); */
/*   fprintf(pic2, "P6\n%i %i\n255\n", effectWidth, effectHeight); */

    for (i = 0; i < effectHeight; i++) {
        p1 = I1;
        p2 = I2;
        if (d_y > 0 ){ 
            p1 += (i + d_y) * width * bytesPerPixel;
            p2 += i * width * bytesPerPixel;
        } else {
            p1 += i * width * bytesPerPixel;
            p2 += (i - d_y) * width * bytesPerPixel;
        }
        if (d_x > 0) { 
            p1 += d_x * bytesPerPixel;
        } else {
            p2 -= d_x * bytesPerPixel; 
        }
        // TODO: use some mmx or sse stuff here
        for (j = 0; j < effectWidth * bytesPerPixel; j++) {
            /* debugging code continued */
            /* fwrite(p1,1,1,pic1);fwrite(p1,1,1,pic1);fwrite(p1,1,1,pic1);
               fwrite(p2,1,1,pic2);fwrite(p2,1,1,pic2);fwrite(p2,1,1,pic2); 
             */
            sum += abs((int)*p1 - (int)*p2);
            p1++;
            p2++;      
        }
    }
    /*  fclose(pic1);
        fclose(pic2); 
     */
    return sum/((double) effectWidth * effectHeight * bytesPerPixel);
}

/**
   compares a small part of two given images 
   and returns the average absolute difference.
   Field center, size and shift have to be choosen, 
   so that no clipping is required
     
   \param field Field specifies position(center) and size of subimage 
   \param d_x shift in x direction
   \param d_y shift in y direction   
*/
double compareSubImg(unsigned char* const I1, unsigned char* const I2, 
                     const Field* field, 
                     int width, int height, int bytesPerPixel, int d_x, int d_y)
{
    int k, j;
    unsigned char* p1 = NULL;
    unsigned char* p2 = NULL;
    int s2 = field->size / 2;
    double sum = 0;

    p1=I1 + ((field->x - s2) + (field->y - s2)*width)*bytesPerPixel;
    p2=I2 + ((field->x - s2 + d_x) + (field->y - s2 + d_y)*width)*bytesPerPixel;
    // TODO: use some mmx or sse stuff here
    for (j = 0; j < field->size; j++){
        for (k = 0; k < field->size * bytesPerPixel; k++) {
            sum += abs((int)*p1 - (int)*p2);
            p1++;
            p2++;     
        }
        p1 += (width - field->size) * bytesPerPixel;
        p2 += (width - field->size) * bytesPerPixel;
    }
    return sum/((double) field->size *field->size* bytesPerPixel);
}

/** \see contrastSubImg called with bytesPerPixel=1*/
double contrastSubImgYUV(StabData* sd, const Field* field){
    return contrastSubImg(sd->curr,field,sd->width,sd->height,1);
}

/** 
    \see contrastSubImg three times called with bytesPerPixel=3 
    for all channels   
 */
double contrastSubImgRGB(StabData* sd, const Field* field){
    unsigned char* const I = sd->curr;
    return (  contrastSubImg(I,  field,sd->width,sd->height,3) 
            + contrastSubImg(I+1,field,sd->width,sd->height,3)
            + contrastSubImg(I+2,field,sd->width,sd->height,3))/3;
}

/**
   calculates Michelson-contrast in the given small part of the given image
     
   \param I pointer to framebuffer 
   \param field Field specifies position(center) and size of subimage 
   \param width width of frame
   \param height height of frame
   \param bytesPerPixel calc contrast for only for first channel
*/
double contrastSubImg(unsigned char* const I, const Field* field, 
                     int width, int height, int bytesPerPixel)
{
    int k, j;
    unsigned char* p = NULL;
    int s2 = field->size / 2;
    unsigned char mini = 255;
    unsigned char maxi = 0;

    p = I + ((field->x - s2) + (field->y - s2)*width)*bytesPerPixel;
    // TODO: use some mmx or sse stuff here
    for (j = 0; j < field->size; j++){
        for (k = 0; k < field->size * bytesPerPixel; k++) {
            mini = (mini < *p) ? mini : *p;
            maxi = (maxi > *p) ? maxi : *p;
            p += bytesPerPixel;
        }
        p += (width - field->size) * bytesPerPixel;
    }
    return (maxi-mini)/(maxi+mini+0.1); // +0.1 to avoid division by 0
}

/** tries to register current frame onto previous frame.
    This is the most simple algorithm:
    shift images to all possible positions and calc summed error
    Shift with minimal error is selected.
*/
Transform calcShiftRGBSimple(StabData* sd)
{
    int x = 0, y = 0;
    int i, j;
    double minerror = 1e20;  
    for (i = -sd->maxshift; i <= sd->maxshift; i++) {
        for (j = -sd->maxshift; j <= sd->maxshift; j++) {
            double error = compareImg(sd->curr, sd->prev, 
                                      sd->width, sd->height, 3, i, j);
            if (error < minerror) {
                minerror = error;
                x = i;
                y = j;
           }	
        }
    } 
    return new_transform(x, y, 0, 0, 0);
}


/** tries to register current frame onto previous frame. 
    (only the luminance is used)
    This is the most simple algorithm:
    shift images to all possible positions and calc summed error
    Shift with minimal error is selected.
*/
Transform calcShiftYUVSimple(StabData* sd)
{
    int x = 0, y = 0;
    int i, j;
    unsigned char *Y_c, *Y_p;// , *Cb, *Cr;
#ifdef STABVERBOSE
    FILE *f = NULL;
    char buffer[32];
    tc_snprintf(buffer, sizeof(buffer), "f%04i.dat", sd->t);
    f = fopen(buffer, "w");
    fprintf(f, "# splot \"%s\"\n", buffer);
#endif

    // we only use the luminance part of the image
    Y_c  = sd->curr;  
    //  Cb_c = sd->curr + sd->width*sd->height;
    //Cr_c = sd->curr + 5*sd->width*sd->height/4;
    Y_p  = sd->prev;  
    //Cb_p = sd->prev + sd->width*sd->height;
    //Cr_p = sd->prev + 5*sd->width*sd->height/4;

    double minerror = 1e20;  
    for (i = -sd->maxshift; i <= sd->maxshift; i++) {
        for (j = -sd->maxshift; j <= sd->maxshift; j++) {
            double error = compareImg(Y_c, Y_p, 
                                      sd->width, sd->height, 1, i, j);
#ifdef STABVERBOSE
            fprintf(f, "%i %i %f\n", i, j, error);
#endif
            if (error < minerror) {
                minerror = error;
                x = i;
                y = j;
            }	
        }
    }  
#ifdef STABVERBOSE
    fclose(f);
    tc_log_msg(MOD_NAME, "Minerror: %f\n", minerror);
#endif
    return new_transform(x, y, 0, 0, 0);
}



/* calculates rotation angle for the given transform and 
 * field with respect to the given center-point
 */
double calcAngle(StabData* sd, Field* field, Transform* t, 
                 int center_x, int center_y)
{
    // we better ignore fields that are to close to the rotation center 
    if (abs(field->x - center_x) + abs(field->y - center_y) < sd->maxshift) {
        return 0;
    } else {
        // double r = sqrt(field->x*field->x + field->y*field->y);   
        double a1 = atan2(field->y - center_y, field->x - center_x);
        double a2 = atan2(field->y - center_y + t->y, 
                          field->x - center_x + t->x);
        double diff = a2 - a1;
        return (diff>M_PI) ? diff - 2*M_PI 
            : ( (diff<-M_PI) ? diff + 2*M_PI : diff);    
    }
}


/* calculates the optimal transformation for one field in YUV frames
 * (only luminance)
 */
Transform calcFieldTransYUV(StabData* sd, const Field* field, int fieldnum)
{
    Transform t = null_transform();
    uint8_t *Y_c = sd->curr, *Y_p = sd->prev;
    // we only use the luminance part of the image
    int i, j;

/*     // check contrast in sub image */
/*     double contr = contrastSubImg(Y_c, field, sd->width, sd->height, 1); */
/*     if(contr < sd->contrast_threshold) { */
/*         t.extra=-1; */
/*         return t; */
/*     } */
#ifdef STABVERBOSE
    // printf("%i %i %f\n", sd->t, fieldnum, contr);    
    FILE *f = NULL;
    char buffer[32];
    tc_snprintf(buffer, sizeof(buffer), "f%04i_%02i.dat", sd->t, fieldnum);
    f = fopen(buffer, "w");
    fprintf(f, "# splot \"%s\"\n", buffer);
#endif    

    double minerror = 1e10;  
    double error = 1e10;
    for (i = -sd->maxshift; i <= sd->maxshift; i += sd->stepsize) {
        for (j = -sd->maxshift; j <= sd->maxshift; j += sd->stepsize) {
            error = compareSubImg(Y_c, Y_p, field, 
                                         sd->width, sd->height, 1, i, j);
#ifdef STABVERBOSE
            fprintf(f, "%i %i %f\n", i, j, error);
#endif 
            if (error < minerror) {
                minerror = error;
                t.x = i;
                t.y = j;
            }	
        }
    }

    if (sd->stepsize > 1) {    // make fine grain check around the best match
        int r = sd->stepsize - 1;
        for (i = t.x - r; i <= t.x + r; i += 1) {
            for (j = -t.y - r; j <= t.y + r; j += 1) {
                if (i == t.x && j == t.y) 
                    continue; //no need to check this since already done
                error = compareSubImg(Y_c, Y_p, field, 
                                      sd->width, sd->height, 1, i, j);
#ifdef STABVERBOSE
                fprintf(f, "%i %i %f\n", i, j, error);
#endif 	
                if (error < minerror){
                    minerror = error;
                    t.x = i;
                    t.y = j;
                }	
            }
        }
    }
#ifdef STABVERBOSE 
    fclose(f); 
    tc_log_msg(MOD_NAME, "Minerror: %f\n", minerror);
#endif

    if (!sd->allowmax && fabs(t.x) == sd->maxshift) {
#ifdef STABVERBOSE 
        tc_log_msg(MOD_NAME, "maximal x shift ");
#endif
        t.x = 0;
    }
    if (!sd->allowmax && fabs(t.y) == sd->maxshift) {
#ifdef STABVERBOSE 
        tc_log_msg(MOD_NAME, "maximal y shift ");
#endif
        t.y = 0;
    }
    return t;
}

/* calculates the optimal transformation for one field in RGB 
 *   slower than the YUV version because it uses all three color channels
 */
Transform calcFieldTransRGB(StabData* sd, const Field* field, int fieldnum)
{
    Transform t = null_transform();
    uint8_t *I_c = sd->curr, *I_p = sd->prev;
    int i, j;
  
    double minerror = 1e20;  
    for (i = -sd->maxshift; i <= sd->maxshift; i += 2) {
        for (j=-sd->maxshift; j <= sd->maxshift; j += 2) {      
            double error = compareSubImg(I_c, I_p, field, 
                                         sd->width, sd->height, 3, i, j);
            if (error < minerror) {
                minerror = error;
                t.x = i;
                t.y = j;
            }	
        }
    }
    for (i = t.x - 1; i <= t.x + 1; i += 2) {
        for (j = -t.y - 1; j <= t.y + 1; j += 2) {
            double error = compareSubImg(I_c, I_p, field, 
                                         sd->width, sd->height, 3, i, j);
            if (error < minerror) {
                minerror = error;
                t.x = i;
                t.y = j;
            }	
        }
    }
    if (!sd->allowmax && fabs(t.x) == sd->maxshift) {
        t.x = 0;
    }
    if (!sd->allowmax && fabs(t.y) == sd->maxshift) {
        t.y = 0;
    }
    return t;
}

/* compares contrast_idx structures respect to the contrast 
   (for sort function) 
*/
int cmp_contrast_idx(const void *ci1, const void* ci2)
{
    double a = ((contrast_idx*)ci1)->contrast;
    double b = ((contrast_idx*)ci2)->contrast;
    return a < b ? 1 : ( a > b ? -1 : 0 );
}

/* select only the best 'maxfields' fields
   first calc contrasts then select from each part of the
   frame a some fields
*/
TCList* selectfields(StabData* sd, contrastSubImgFunc contrastfunc)
{
    int i,j;
    TCList* goodflds = tc_list_new(0);
    contrast_idx *ci = tc_malloc(sizeof(contrast_idx) * sd->field_num);
    
    // we split all fields into row+1 segments and take from each segment
    // the best fields
    int numsegms = (sd->field_rows+1);
    int segmlen = sd->field_num/(sd->field_rows+1)+1;
    // split the frame list into rows+1 segments
    contrast_idx *ci_segms = tc_malloc(sizeof(contrast_idx) * sd->field_num);
    int remaining   = 0;
    // calculate contrast for each field
    for (i = 0; i < sd->field_num; i++) {        
        ci[i].contrast = contrastfunc(sd, &sd->fields[i]);
        ci[i].index=i;
        if(ci[i].contrast < sd->contrast_threshold) ci[i].contrast = 0;
        // else printf("%i %lf\n", ci[i].index, ci[i].contrast);
    }   

    memcpy(ci_segms, ci, sizeof(contrast_idx) * sd->field_num);
    // get best fields from each segment
    for(i=0; i<numsegms; i++){
        int startindex = segmlen*i;
        int endindex   = segmlen*(i+1);
        endindex       = endindex > sd->field_num ? sd->field_num : endindex;
        //printf("Segment: %i: %i-%i\n", i, startindex, endindex);

        // sort within segment
        qsort(ci_segms+startindex, endindex-startindex, 
              sizeof(contrast_idx), cmp_contrast_idx);        
        // take maxfields/numsegms
        for(j=0; j<sd->maxfields/numsegms; j++){
            if(startindex+j >= endindex) continue;
            // printf("%i %lf\n", ci_segms[startindex+j].index, 
            //                    ci_segms[startindex+j].contrast);
            if(ci_segms[startindex+j].contrast > 0){                
                tc_list_append_dup(goodflds, &ci[ci_segms[startindex+j].index],
                                   sizeof(contrast_idx));
                // don't consider them in the later selection process
                ci_segms[startindex+j].contrast=0; 
            }                                                     
        }
    }
    // check whether enough fields are selected
    // printf("Phase2: %i\n", tc_list_size(goodflds));
    remaining = sd->maxfields - tc_list_size(goodflds); 
    if(remaining > 0){
        // take the remaining from the leftovers
        qsort(ci_segms, sd->field_num,                   
              sizeof(contrast_idx), cmp_contrast_idx);
        for(j=0; j < remaining; j++){
            if(ci_segms[j].contrast > 0){
                tc_list_append_dup(goodflds, &ci_segms[j], sizeof(contrast_idx));                    
            }                                                     
        }
    }     
    // printf("Ende: %i\n", tc_list_size(goodflds));
    tc_free(ci);
    tc_free(ci_segms);
    return goodflds;
}



/* tries to register current frame onto previous frame. 
 *   Algorithm:
 *   check all fields for vertical and horizontal transformation
 *   use minimal difference of all possible positions
 *   discards fields with low contrast 
 *   select maxfields field according to their contrast
 *   calculate shift as cleaned mean of all remaining fields
 *   calculate rotation angle of each field in respect to center of fields
 *   after shift removal
 *   calculate rotation angle as cleaned mean of all angles
 *   compensate for possibly off-center rotation
*/
Transform calcTransFields(StabData* sd, calcFieldTransFunc fieldfunc,
                          contrastSubImgFunc contrastfunc)
{
    Transform* ts  = tc_malloc(sizeof(Transform) * sd->field_num);
    Field** fs     = tc_malloc(sizeof(Field*) * sd->field_num);
    double *angles = tc_malloc(sizeof(double) * sd->field_num);
    int i, index=0, num_trans;
    Transform t;
#ifdef STABVERBOSE
    FILE *f = NULL;
    char buffer[32];
    tc_snprintf(buffer, sizeof(buffer), "k%04i.dat", sd->t);
    f = fopen(buffer, "w");
    fprintf(f, "# plot \"%s\" w l, \"\" every 2:1:0\n", buffer);
#endif
    
    TCList* goodflds = selectfields(sd, contrastfunc);

    // use all "good" fields and calculate optimal match to previous frame 
    contrast_idx* f;
    while((f = (contrast_idx*)tc_list_pop(goodflds,0)) != 0){
        int i = f->index;
        t =  fieldfunc(sd, &sd->fields[i], i); // e.g. calcFieldTransYUV
#ifdef STABVERBOSE
        fprintf(f, "%i %i\n%f %f %i\n \n\n", sd->fields[i].x, sd->fields[i].y, 
                sd->fields[i].x + t.x, sd->fields[i].y + t.y, t.extra);
#endif
        if (t.extra != -1){ // ignore if extra == -1 (unused at the moment)
            ts[index] = t;
            fs[index] = sd->fields+i;
            index++;
        }
    }
    tc_list_fini(goodflds);

    t = null_transform();
    num_trans = index; // amount of transforms we actually have    
    if (num_trans < 1) {
        tc_log_warn(MOD_NAME, "too low contrast! No field remains.\n \
                    (no translations are detected in frame %i)", sd->t);
        return t;
    }
        
    int center_x = 0;
    int center_y = 0;
    // calc center point of all remaining fields
    for (i = 0; i < num_trans; i++) {
        center_x += fs[i]->x;
        center_y += fs[i]->y;            
    } 
    center_x /= num_trans;
    center_y /= num_trans;        
    
    if (sd->show){ // draw fields and transforms into frame.
        // this has to be done one after another to handle possible overlap 
        if (sd->show > 1) {
            for (i = 0; i < num_trans; i++)
                drawFieldScanArea(sd, fs[i], &ts[i]);            
        }
        for (i = 0; i < num_trans; i++)
            drawField(sd, fs[i], &ts[i]);            
        for (i = 0; i < num_trans; i++)
            drawFieldTrans(sd, fs[i], &ts[i]);            
    } 
    /* median over all transforms
       t= median_xy_transform(ts, sd->field_num);*/
    // cleaned mean    
    t = cleanmean_xy_transform(ts, num_trans);

    // substract avg
    for (i = 0; i < num_trans; i++) {
        ts[i] = sub_transforms(&ts[i], &t);
    }
    // figure out angle
    if (sd->field_num < 6) {
        // the angle calculation is inaccurate for 5 and less fields
        t.alpha = 0; 
    } else {      
        for (i = 0; i < num_trans; i++) {
            angles[i] = calcAngle(sd, fs[i], &ts[i], center_x, center_y);
        }
        double min,max;
        t.alpha = -cleanmean(angles, num_trans, &min, &max);
        if(max-min>sd->maxanglevariation){
            t.alpha=0;
            tc_log_info(MOD_NAME, "too large variation in angle(%f)\n", 
                        max-min);
        }
    }
    // compensate for off-center rotation
    double p_x = (center_x - sd->width/2);
    double p_y = (center_y - sd->height/2);
    t.x += (cos(t.alpha)-1)*p_x  - sin(t.alpha)*p_y;
    t.y += sin(t.alpha)*p_x  + (cos(t.alpha)-1)*p_y;    
    
#ifdef STABVERBOSE
    fclose(f);
#endif
    return t;
}

/** draws the field scanning area */
void drawFieldScanArea(StabData* sd, const Field* field, const Transform* t)
{
    if (!sd->vob->im_v_codec == TC_CODEC_YUV420P)
        return;
    drawBox(sd->curr, sd->width, sd->height, 1, field->x, field->y, 
            field->size+2*sd->maxshift, field->size+2*sd->maxshift, 80);   
}

/** draws the field */
void drawField(StabData* sd, const Field* field, const Transform* t)
{
    if (!sd->vob->im_v_codec == TC_CODEC_YUV420P)
        return;
    drawBox(sd->curr, sd->width, sd->height, 1, field->x, field->y, 
            field->size, field->size, t->extra == -1 ? 100 : 40);
}

/** draws the transform data of this field */
void drawFieldTrans(StabData* sd, const Field* field, const Transform* t)
{
    if (!sd->vob->im_v_codec == TC_CODEC_YUV420P)
        return;
    drawBox(sd->curr, sd->width, sd->height, 1, 
            field->x, field->y, 5, 5, 128);     // draw center
    drawBox(sd->curr, sd->width, sd->height, 1, 
            field->x + t->x, field->y + t->y, 8, 8, 250); // draw translation
}

/**
 * draws a box at the given position x,y (center) in the given color
   (the same for all channels) 
 */
void drawBox(unsigned char* I, int width, int height, int bytesPerPixel, 
             int x, int y, int sizex, int sizey, unsigned char color){
    
    unsigned char* p = NULL;     
    int j,k;
    p = I + ((x - sizex/2) + (y - sizey/2)*width)*bytesPerPixel;
    for (j = 0; j < sizey; j++){
        for (k = 0; k < sizex * bytesPerPixel; k++) {
            *p = color;
            p++;
        }
        p += (width - sizex) * bytesPerPixel;
    }
}

struct iterdata {
    FILE *f;
    int  counter;
};

static int stabilize_dump_trans(TCListItem *item, void *userdata)
{
    struct iterdata *ID = userdata;

    if (item->data) {
        Transform* t = item->data;
        fprintf(ID->f, "%i %6.4lf %6.4lf %8.5lf %6.4lf %i\n",
                ID->counter, t->x, t->y, t->alpha, t->zoom, t->extra);
        ID->counter++;
    }
    return 0; /* never give up */
}

/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * stabilize_init:  Initialize this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int stabilize_init(TCModuleInstance *self, uint32_t features)
{
    StabData* sd = NULL;
    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    sd = tc_zalloc(sizeof(StabData)); // allocation with zero values
    if (!sd) {
        if (verbose > TC_INFO)
            tc_log_error(MOD_NAME, "init: out of memory!");
        return TC_ERROR;
    }

    sd->vob = tc_get_vob();
    if (!sd->vob)
        return TC_ERROR;

    /**** Initialise private data structure */

    self->userdata = sd;
    if (verbose & TC_INFO){
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }

    return TC_OK;
}


/*
 * stabilize_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */
static int stabilize_fini(TCModuleInstance *self)
{
    StabData *sd = NULL;
    TC_MODULE_SELF_CHECK(self, "fini");
    sd = self->userdata;

    tc_free(sd);
    self->userdata = NULL;
    return TC_OK;
}

/*
 * stabilize_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */
static int stabilize_configure(TCModuleInstance *self,
            			       const char *options,
                               TCJob *vob,
                               TCModuleExtraData *xdata[])
{
    StabData *sd = NULL;
    TC_MODULE_SELF_CHECK(self, "configure");
    char* filenamecopy, *filebasename;

    sd = self->userdata;

    /*    sd->framesize = sd->vob->im_v_width * MAX_PLANES * 
          sizeof(char) * 2 * sd->vob->im_v_height * 2;     */
    sd->framesize = sd->vob->im_v_size;    
    sd->prev = tc_zalloc(sd->framesize);    
    if (!sd->prev) {
        tc_log_error(MOD_NAME, "malloc failed");
        return TC_ERROR;
    }
    sd->currcopy = 0;

    sd->width  = sd->vob->ex_v_width;
    sd->height = sd->vob->ex_v_height;

    sd->hasSeenOneFrame = 0;
    sd->transs = 0;
    
    // Options
    sd->stepsize   = 6;
    sd->allowmax   = 0;
    sd->result = tc_malloc(TC_BUF_LINE);
    filenamecopy = tc_strdup(sd->vob->video_in_file);
    filebasename = basename(filenamecopy);
    if (strlen(filebasename) < TC_BUF_LINE - 4) {
        tc_snprintf(sd->result, TC_BUF_LINE, "%s.trf", filebasename);
    } else {
        tc_log_warn(MOD_NAME, "input name too long, using default `%s'",
                    DEFAULT_TRANS_FILE_NAME);
        tc_snprintf(sd->result, TC_BUF_LINE, DEFAULT_TRANS_FILE_NAME);
    }
    sd->algo = 1;
//    sd->field_num   = 64;
    sd->accuracy    = 4;
    sd->shakiness   = 4;
    sd->field_size  = TC_MIN(sd->width, sd->height)/12;
    sd->show        = 0;
    sd->contrast_threshold = 0.3; 
    sd->maxanglevariation = 1;
    
    if (options != NULL) {            
        // for some reason this plugin is called in the old fashion 
        //  (not with inspect). Anyway we support both ways of getting help.
        if(optstr_lookup(options, "help")) {
            tc_log_info(MOD_NAME,stabilize_help);
            return(TC_IMPORT_ERROR);
        }

        optstr_get(options, "result",     "%[^:]", sd->result);
        optstr_get(options, "shakiness",  "%d", &sd->shakiness);
        optstr_get(options, "accuracy",   "%d", &sd->accuracy);
        optstr_get(options, "stepsize",   "%d", &sd->stepsize);
        optstr_get(options, "algo",       "%d", &sd->algo);
        optstr_get(options, "mincontrast","%lf",&sd->contrast_threshold);
        optstr_get(options, "show",       "%d", &sd->show);
    }
    sd->shakiness = TC_MIN(10,TC_MAX(1,sd->shakiness));
    sd->accuracy  = TC_MAX(sd->shakiness,TC_MIN(15,TC_MAX(1,sd->accuracy)));
    if (verbose) {
        tc_log_info(MOD_NAME, "Image Stabilization Settings:");
        tc_log_info(MOD_NAME, "     shakiness = %d", sd->shakiness);
        tc_log_info(MOD_NAME, "      accuracy = %d", sd->accuracy);
        tc_log_info(MOD_NAME, "      stepsize = %d", sd->stepsize);
        tc_log_info(MOD_NAME, "          algo = %d", sd->algo);
        tc_log_info(MOD_NAME, "   mincontrast = %f", sd->contrast_threshold);
        tc_log_info(MOD_NAME, "          show = %d", sd->show);
        tc_log_info(MOD_NAME, "        result = %s", sd->result);
    }

    // shift and size: shakiness 1: height/40; 10: height/4
    sd->maxshift    = TC_MIN(sd->width, sd->height)*sd->shakiness/40;
    sd->field_size   = TC_MIN(sd->width, sd->height)*sd->shakiness/40;
  
    tc_log_info(MOD_NAME, "Fieldsize: %i, Maximal translation: %i pixel", 
                sd->field_size, sd->maxshift);
    if (sd->algo==1) {        
        // initialize measurement fields. field_num is set here.
        if (!initFields(sd)) {
            return TC_ERROR;
        }
        sd->maxfields = (sd->accuracy) * sd->field_num / 15;
        tc_log_info(MOD_NAME, "Number of used measurement fields: %i out of %i",
                    sd->maxfields, sd->field_num);
    }
    sd->f = fopen(sd->result, "w");
    if (sd->f == NULL) {
        tc_log_error(MOD_NAME, "cannot open result file %s!\n", sd->result);
        return TC_ERROR;
    }    
    if (sd->show)
        sd->currcopy = tc_zalloc(sd->framesize);

    /* load unsharp filter to smooth the frames. This allows larger stepsize.*/
    char unsharp_param[128];
    int masksize = TC_MIN(13,sd->stepsize*1.8); // only works up to 13.
    sprintf(unsharp_param,"luma=-1:luma_matrix=%ix%i:pre=1", 
            masksize, masksize);
    if (!tc_filter_add("unsharp", unsharp_param)) {
        tc_log_warn(MOD_NAME, "cannot load unsharp filter!");
    }

    return TC_OK;
}


/**
 * stabilize_filter_video: performs the analysis of subsequent frames
 * See tcmodule-data.h for function details.
 */

static int stabilize_filter_video(TCModuleInstance *self, 
                                  vframe_list_t *frame)
{
    StabData *sd = NULL;
  
    TC_MODULE_SELF_CHECK(self, "filter_video");
    TC_MODULE_SELF_CHECK(frame, "filter_video");
  
    sd = self->userdata;    

    if(sd->show)  // save the buffer to restore at the end for prev
        memcpy(sd->currcopy, frame->video_buf, sd->framesize);
    
    if (sd->hasSeenOneFrame) {
        sd->curr = frame->video_buf;
        if (sd->vob->im_v_codec == TC_CODEC_RGB24) {
            if (sd->algo == 0)
                addTrans(sd, calcShiftRGBSimple(sd));
            else if (sd->algo == 1)
                addTrans(sd, calcTransFields(sd, calcFieldTransRGB,
                         contrastSubImgRGB));
        } else if (sd->vob->im_v_codec == TC_CODEC_YUV420P) {
            if (sd->algo == 0)
                addTrans(sd, calcShiftYUVSimple(sd));
            else if (sd->algo == 1)
                addTrans(sd, calcTransFields(sd, calcFieldTransYUV,
                                             contrastSubImgYUV));
        } else {
            tc_log_warn(MOD_NAME, "unsupported Codec: %i\n",
                        sd->vob->im_v_codec);
            return TC_ERROR;
        }
    } else {
        sd->hasSeenOneFrame = 1;
        addTrans(sd, null_transform());
    }
    
    if(!sd->show) { // copy current frame to prev for next frame comparison
        memcpy(sd->prev, frame->video_buf, sd->framesize);
    } else { // use the copy because we changed the original frame
        memcpy(sd->prev, sd->currcopy, sd->framesize);
    }
    sd->t++;
    return TC_OK;
}

/**
 * stabilize_stop:  Reset this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int stabilize_stop(TCModuleInstance *self)
{
    StabData *sd = NULL;
    TC_MODULE_SELF_CHECK(self, "stop");
    sd = self->userdata;

    // print transs
    if (sd->f) {
        struct iterdata ID;
        ID.counter = 0;
        ID.f       = sd->f;
        // write parameters as comments to file 
        fprintf(sd->f, "#      accuracy = %d\n", sd->accuracy);
        fprintf(sd->f, "#     shakiness = %d\n", sd->shakiness);
        fprintf(sd->f, "#      stepsize = %d\n", sd->stepsize);
        fprintf(sd->f, "#          algo = %d\n", sd->algo);
        fprintf(sd->f, "#   mincontrast = %f\n", sd->contrast_threshold);
        fprintf(sd->f, "#        result = %s\n", sd->result);
        // write header line
        fprintf(sd->f, "# Transforms\n#C FrameNr x y alpha zoom extra\n");
        // and all transforms
        tc_list_foreach(sd->transs, stabilize_dump_trans, &ID);
    
        fclose(sd->f);
        sd->f = NULL;
    }
    tc_list_del(sd->transs, 1 );
    if (sd->prev) {
        tc_free(sd->prev);
        sd->prev = NULL;
    }
    if (sd->result) {
        tc_free(sd->result);
        sd->result = NULL;
    }
    return TC_OK;
}

/* checks for parameter in function _inspect */
#define CHECKPARAM(paramname, formatstring, variable)       \
    if (optstr_lookup(param, paramname)) {                \
        tc_snprintf(sd->conf_str, sizeof(sd->conf_str),   \
                    formatstring, variable);         \
        *value = sd->conf_str;                            \
    }

/**
 * stabilize_inspect:  Return the value of an option in this instance of
 * the module.  See tcmodule-data.h for function details.
 */

static int stabilize_inspect(TCModuleInstance *self,
			     const char *param, const char **value)
{
    StabData *sd = NULL;
    
    TC_MODULE_SELF_CHECK(self, "inspect");
    TC_MODULE_SELF_CHECK(param, "inspect");
    TC_MODULE_SELF_CHECK(value, "inspect");
    sd = self->userdata;

    if (optstr_lookup(param, "help")) {
        *value = stabilize_help;
    }
    CHECKPARAM("shakiness","shakiness=%d", sd->shakiness);
    CHECKPARAM("accuracy", "accuracy=%d",  sd->accuracy); 
    CHECKPARAM("stepsize", "stepsize=%d",  sd->stepsize);
    CHECKPARAM("allowmax", "allowmax=%d",  sd->allowmax);
    CHECKPARAM("algo",     "algo=%d",      sd->algo);
    CHECKPARAM("result",   "result=%s",    sd->result);
    return TC_OK;
}

static const TCCodecID stabilize_codecs_video_in[] = { 
    TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_RGB24, TC_CODEC_ERROR 
};
static const TCCodecID stabilize_codecs_video_out[] = { 
    TC_CODEC_YUV420P, TC_CODEC_YUV422P, TC_CODEC_RGB24, TC_CODEC_ERROR 
};
TC_MODULE_AUDIO_UNSUPPORTED(stabilize);
TC_MODULE_FILTER_FORMATS(stabilize); 

TC_MODULE_INFO(stabilize);

static const TCModuleClass stabilize_class = {
    TC_MODULE_CLASS_HEAD(stabilize),

    .init         = stabilize_init,
    .fini         = stabilize_fini,
    .configure    = stabilize_configure,
    .stop         = stabilize_stop,
    .inspect      = stabilize_inspect,

    .filter_video = stabilize_filter_video,
};

TC_MODULE_ENTRY_POINT(stabilize)

/*************************************************************************/

static int stabilize_get_config(TCModuleInstance *self, char *options)
{
    TC_MODULE_SELF_CHECK(self, "get_config");

    optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
                       MOD_AUTHOR, "VRY4", "1");

    return TC_OK;
}

static int stabilize_process(TCModuleInstance *self, frame_list_t *frame)
{
    TC_MODULE_SELF_CHECK(self, "process");

    if (frame->tag & TC_POST_S_PROCESS && frame->tag & TC_VIDEO) {
        return stabilize_filter_video(self, (vframe_list_t *)frame);
    }
    return TC_OK;
}

/*************************************************************************/

TC_FILTER_OLDINTERFACE(stabilize)

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
