/*
 * 1394-Based Digital Camera Capture Code for the Control Library
 *
 * Written by Chris Urmson <curmson@ri.cmu.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "dc1394_control.h"
#include <video1394.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>


#define NUM_ISO_CHANNELS 64
/*Variables used for simultaneous capture of video from muliple cameras*/
int * _dc1394_buffer[NUM_ISO_CHANNELS];
int _dc1394_frame_captured[NUM_ISO_CHANNELS];
int _dc1394_offset[NUM_ISO_CHANNELS];
int _dc1394_quadlets_per_frame[NUM_ISO_CHANNELS];
int _dc1394_quadlets_per_packet[NUM_ISO_CHANNELS];
int _dc1394_all_captured;

/**********************/
/* Internal functions */
/**********************/

/*****************************************/
/* Functions defined in dc1394_control.c */
/*****************************************/
int
_dc1394_get_wh_from_format(int format, int mode, int *w, int *h);

int 
_dc1394_get_quadlets_per_packet(int format,int mode, int frame_rate); 

int 
_dc1394_quadlets_from_format(int format, int mode);

/*****************************************************
_dc1394_video_iso_handler
this is the routine that is plugged into the raw1394 callback
hook to allow us to capture mutliple iso video streams.  This
is used in the non DMA capture routines.
*****************************************************/
int 
_dc1394_video_iso_handler(raw1394handle_t handle,  
			      int channel,size_t length,quadlet_t *data) 
{

    /*the first packet of a frame has a 1 in the lsb of the header */
    if ((data[0] & 0x1) && (_dc1394_frame_captured[channel] !=1))
    {
        _dc1394_offset[channel] =0;
        _dc1394_frame_captured[channel]=2;

        /* the plus 1 is to shift past the first header quadlet*/
        memcpy((char*)_dc1394_buffer[channel],(char*)(data+1),
               4*_dc1394_quadlets_per_packet[channel]);
        _dc1394_offset[channel]+=_dc1394_quadlets_per_packet[channel];
/*        printf("ch: %d:\n",channel);
          printf("%08x\n%08x\n%08x\n%08x\n",data[0],data[1],data[2],data[3]);*/
        
    } else if (_dc1394_frame_captured[channel] == 2) 
    {
        memcpy((char*)(_dc1394_buffer[channel]+_dc1394_offset[channel]),
               (char*)(data+1),4*_dc1394_quadlets_per_packet[channel]);

        _dc1394_offset[channel]+=_dc1394_quadlets_per_packet[channel];

        if (_dc1394_offset[channel]>=_dc1394_quadlets_per_frame[channel]) 
        {
            _dc1394_frame_captured[channel]=1;      
            _dc1394_all_captured--;     
            _dc1394_offset[channel]=0;
      
        }
    
    }
    //  printf("offset is: %d\n",offset);
    return 1;
}

/*****************************************************
_dc1394_basic_setup
sets up camera features that are capture type independent
returns DC1394_SUCCESS on success, DC1394_FAILURE otherwise
*****************************************************/
int 
_dc1394_basic_setup(raw1394handle_t handle, nodeid_t node, 
                       int channel, int format, int mode, 
                       int speed, int frame_rate, 
                       dc1394_cameracapture * camera) {
if (dc1394_init_camera(handle,node)!=DC1394_SUCCESS) 
    {
        printf("(%s) Unable to initialize camera!\n",__FILE__);
        return DC1394_FAILURE;
    }
    if (dc1394_set_video_format(handle,node,format)!=DC1394_SUCCESS) 
    {
        printf("(%s) Unable to set video format %d!\n",__FILE__,format);
        return DC1394_FAILURE;
    }
    if (dc1394_set_video_mode(handle, node,mode)!=DC1394_SUCCESS) 
    {
        printf("(%s) Unable to set video mode %d!\n",__FILE__, mode);
        return DC1394_FAILURE;
    }
    if (dc1394_set_video_framerate(handle,node,frame_rate)!=DC1394_SUCCESS) 
    {
        printf("(%s) Unable to set framerate %d!\n",__FILE__,frame_rate);
        return DC1394_FAILURE;
    }
    if (dc1394_set_iso_channel_and_speed(handle,node,channel,speed)!=DC1394_SUCCESS) 
    {
        printf("(%s) Unable to set channel %d and speed %d!\n",__FILE__,channel,speed);
        return DC1394_FAILURE;
    }
    camera->node = node;
    camera->frame_rate = frame_rate;
    camera->channel=channel;
    camera->quadlets_per_packet = _dc1394_get_quadlets_per_packet(format,mode,
                                                                  frame_rate);

    if (camera->quadlets_per_packet<0) 
    {
        return DC1394_FAILURE;
    }
  
    camera->quadlets_per_frame= _dc1394_quadlets_from_format(format,mode);
    if (camera->quadlets_per_frame<0) 
    {
        return DC1394_FAILURE;
    }
    if (_dc1394_get_wh_from_format(format,mode,&camera->frame_width,
                                   &camera->frame_height)==DC1394_FAILURE) 
    {
        return DC1394_FAILURE;
    }


    return DC1394_SUCCESS;
}




/*****************************
libraw Capture Functions 
These functions use libraw
to grab frames from the cameras,
the dma routines are faster, and 
should be used instead.
*****************************/

/*****************************************************
dc1394_setup_camera
sets up both the camera and the cameracapture structure
to be used other places.  
returns DC1394_SUCCESS on success, DC1394_FAILURE otherwise
*****************************************************/
int 
dc1394_setup_capture(raw1394handle_t handle, nodeid_t node, 
                    int channel, int format, int mode, 
                    int speed, int frame_rate, 
                    dc1394_cameracapture * camera) 
{
  if (_dc1394_basic_setup(handle,node, channel, format, mode, 
                          speed,frame_rate, camera)== DC1394_FAILURE)
    {
        return DC1394_FAILURE;
    }
    
    
    camera->capture_buffer =(int *) malloc(camera->quadlets_per_frame*4);

    if (camera->capture_buffer==NULL) 
    {
        printf("(%s) unable to allocate memory for capture buffer\n",__FILE__);
        return DC1394_FAILURE;
    }
    return DC1394_SUCCESS;
}



/*****************************************************
dc1394_release_camera
frees buffer space contained in the cameracapture 
structure
*****************************************************/
int 
dc1394_release_camera(raw1394handle_t handle,dc1394_cameracapture *camera) 
{
    dc1394_unset_one_shot(handle,camera->node);
    if (camera->capture_buffer!=NULL) 
    {
        free(camera->capture_buffer);
    }
    return DC1394_SUCCESS;
}

/*****************************************************
dc1394_single_capture
captures a frame of video from the camera specified
*****************************************************/
int 
dc1394_single_capture(raw1394handle_t handle,
                      dc1394_cameracapture *camera)
{
    return dc1394_multi_capture(handle,camera,1);
}


/*****************************************************
dc1394_multi_capture
this routine captures a frame from each camera specified
in the cams array.  Cameras must be set up first using dc1394_setup_camera
returns DC1394_FAILURE if it fails, DC1394_SUCCESS if it scucceeds
*****************************************************/
int 
dc1394_multi_capture(raw1394handle_t handle, dc1394_cameracapture *cams, int num) 
{
    int i,j;
    _dc1394_all_captured=num;
    /*this first routine does the setup-
      sets up the global variables needed in the handler,
      sets the iso handler,
      tells the 1394 subsystem to listen for iso packets
    */
    for (i=0;i<num;i++) 
    {
        _dc1394_buffer[cams[i].channel]=cams[i].capture_buffer;
        if (raw1394_set_iso_handler(handle,cams[i].channel,_dc1394_video_iso_handler)<0) 
        {
            /* error handling- for some reason something didn't work, 
               so we have to reset everything....*/
            printf("(%s:%d) error!\n",__FILE__,__LINE__);
            for (j=i-1;j>-1;j--) 
            {
                raw1394_stop_iso_rcv(handle,cams[j].channel);
                raw1394_set_iso_handler(handle,cams[j].channel,NULL);
            }
            return DC1394_FAILURE;
        }
        _dc1394_frame_captured[cams[i].channel]=0;
        _dc1394_quadlets_per_frame[cams[i].channel]=cams[i].quadlets_per_frame;
        _dc1394_quadlets_per_packet[cams[i].channel]=cams[i].quadlets_per_packet;
        if (raw1394_start_iso_rcv(handle,cams[i].channel)<0) 
        {
            /* error handling- for some reason something didn't work, 
               so we have to reset everything....*/
            printf("(%s:%d) error!\n",__FILE__,__LINE__);
            for (j=0;j<num;j++) 
            {
                raw1394_stop_iso_rcv(handle,cams[j].channel);
                raw1394_set_iso_handler(handle,cams[j].channel,NULL);

            }
            return DC1394_FAILURE;
        }
    }

  
    /* now we iterate till the data is here*/
    while (_dc1394_all_captured!=0) 
    {
        raw1394_loop_iterate(handle);
    }
  
    /* now stop the subsystem from listening*/
    for (i=0;i<num;i++) 
    {
        raw1394_stop_iso_rcv(handle,cams[i].channel);
        raw1394_set_iso_handler(handle,cams[i].channel,NULL);
    }
    return DC1394_SUCCESS;
}


/*****************************
DMA Capture Functions 
These routines will be much faster
than the above capture routines.
*****************************/

/*****************************************************
dc1394_dma_setup_camera
this sets up the given camera to capture images using 
the dma engine.  Should be much faster than the above
routines
*****************************************************/
int
dc1394_dma_setup_capture(raw1394handle_t handle, nodeid_t node,
                        int channel, int format, int mode,
                        int speed, int frame_rate, 
                        int num_dma_buffers,
                        dc1394_cameracapture *camera) 
{
    struct video1394_mmap vmmap;
    struct video1394_wait vwait;
    int i;
    if (_dc1394_basic_setup(handle,node, channel, format, mode, 
                            speed,frame_rate, camera)== DC1394_FAILURE)
    {
        return DC1394_FAILURE;
    }

    if ((camera->dma_fd = open("/dev/video1394",O_RDWR))<0) {
        printf("(%s) unable to open vide1394 device!\n",__FILE__);
        return DC1394_FAILURE;
    }
    vmmap.sync_tag = 1;
    vmmap.nb_buffers = num_dma_buffers;
    vmmap.flags = VIDEO1394_SYNC_FRAMES;
    vmmap.buf_size = camera->quadlets_per_frame*4;//number of bytes needed
    vmmap.channel = channel;

    if (ioctl(camera->dma_fd,VIDEO1394_LISTEN_CHANNEL, &vmmap)<0) {
        printf("(%s) VIDEO1394_LISTEN_CHANNEL ioctl failed!\n",__FILE__);
        return DC1394_FAILURE;
    }
    printf("requested %d bytes buffer, got %d bytes buffer\n",camera->quadlets_per_frame*4,vmmap.buf_size);
    camera->dma_frame_size = vmmap.buf_size;
    camera->num_dma_buffers = vmmap.nb_buffers;
    camera->dma_last_buffer = -1;
    camera->dma_ring_buffer = mmap(0,vmmap.nb_buffers *vmmap.buf_size,
                           PROT_READ|PROT_WRITE,MAP_SHARED, camera->dma_fd,0);


    /* tell the video1394 system that we want to listen to the given channel */
    if (camera->dma_ring_buffer == ((unsigned char *) -1)) {
        printf("(%s) mmap failed!\n",__FILE__);
        ioctl(camera->dma_fd,VIDEO1394_UNLISTEN_CHANNEL, &vmmap.channel);
        return DC1394_FAILURE;
    }
    camera->dma_buffer_size = vmmap.buf_size;
    vwait.channel = channel;
    /* QUEUE the buffers */
    for (i=0;i<vmmap.nb_buffers;i++) {
        vwait.buffer = i;
        if (ioctl(camera->dma_fd,VIDEO1394_LISTEN_QUEUE_BUFFER,&vwait)<0) {
            printf("(%s) VIDEO1394_LISTEN_QUEUE_BUFFER ioctl failed!\n",
                   __FILE__); 
            ioctl(camera->dma_fd,VIDEO1394_UNLISTEN_CHANNEL, &vmmap.channel);
            munmap(camera->dma_ring_buffer,vmmap.buf_size);
            return DC1394_FAILURE;
        }
    }
    return DC1394_SUCCESS;
}


/*****************************************************
dc1394_dma_release_camera
this releases memory that was mapped by
dc1394_dma_setup_camera
*****************************************************/
int 
dc1394_dma_release_camera(dc1394_cameracapture *camera) 
{
    ioctl(camera->dma_fd,VIDEO1394_UNLISTEN_CHANNEL, &camera->channel);
    if (camera->dma_ring_buffer)
        munmap(camera->dma_ring_buffer,camera->dma_buffer_size);
    return DC1394_SUCCESS;
}


/*****************************************************
dc1394_dma_single_capture
This captures a frame from the given camera
*****************************************************/
int 
dc1394_dma_single_capture(dc1394_cameracapture *camera) 
{
    return dc1394_dma_multi_capture(camera,1);

}

/*****************************************************
dc1394_dma_multi_capture
This capture a frame from each of the cameras passed in
cams.  After you are finished with the frame, you must
return the buffer to the pool by calling
dc1394_dma_done_with_buffer.
*****************************************************/
int
dc1394_dma_multi_capture(dc1394_cameracapture *cams, int num) 
{
    struct video1394_wait vwait;
    int i;
    int cb, extra_buf;
    int j;
    for (i=0;i<num;i++) {
        vwait.channel = cams[i].channel;
        cb = (cams[i].dma_last_buffer+1)%cams[i].num_dma_buffers;
        vwait.buffer = cb;
        //        cams[i].dma_last_buffer = cb;
        if (ioctl(cams[i].dma_fd,VIDEO1394_LISTEN_WAIT_BUFFER,&vwait)!=0) 
        {
            printf("(%s) VIDEO1394_LISTEN_WAIT_BUFFER ioctl failed!\n",
                   __FILE__);
            return DC1394_FAILURE;
        }
        
        /* we want to return the most recent buffer, so skip the old 
           ones */
        extra_buf = vwait.buffer;
        if (extra_buf) 
        {
            for (j=0;j<extra_buf;j++) 
            {
                vwait.buffer = (cb+j)%cams[i].num_dma_buffers;         
                if (ioctl(cams[i].dma_fd,VIDEO1394_LISTEN_QUEUE_BUFFER,&vwait)<0)
                {
                    printf("(%s) VIDEO1394_LISTEN_QUEUE_BUFFER failed in "
                           "multi capture!\n",__FILE__);
                }
            }
        }
        cams[i].dma_last_buffer = (cb+extra_buf)%cams[i].num_dma_buffers;
        cams[i].capture_buffer = (int *) (cams[i].dma_ring_buffer + (cams[i].dma_last_buffer)*cams[i].dma_frame_size);
        
    }
    return DC1394_SUCCESS;
}

/*****************************************************
dc1394_dma_done_with_buffer
This allows the driver to use the buffer previously handed
to the user by dc1394_dma_*_capture
*****************************************************/
int 
dc1394_dma_done_with_buffer(dc1394_cameracapture * camera) 
{
    struct video1394_wait vwait;
    
    if (camera->dma_last_buffer == -1) 
        return DC1394_SUCCESS;
    vwait.channel = camera->channel;
    vwait.buffer = camera->dma_last_buffer;
    //    printf("trying to return buffer: %d for channel: %d\n", vwait.buffer, vwait.channel);
    if (ioctl(camera->dma_fd,VIDEO1394_LISTEN_QUEUE_BUFFER,&vwait)<0) 
      {
        printf("(%s) VIDEO1394_LISTEN_QUEUE_BUFFER failed in "
               "done with buffer!\n",__FILE__);
        return DC1394_FAILURE;
      }
    return DC1394_SUCCESS;
}
















