/**************************************************************************
**       Title: grab partial images from camera, measure frame rate
**    $RCSfile$
**   $Revision$$Name$
**       $Date$
**   Copyright: LGPL $Author$
** Description:
**
**    Grab partial image from camera. Camera must be format 7
**    (scalable image size) compatible. e.g., Basler A101f
**
**-------------------------------------------------------------------------
**
**  $Log$
**  Revision 1.1  2001/07/24 13:50:59  ronneber
**  - simple test programs to demonstrate the use of libdc1394 (based
**    on 'samplegrab' of Chris Urmson
**
**
**************************************************************************/

#include <stdio.h>
#include <libraw1394/raw1394.h>
#include <libdc1394/dc1394_control.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[]) 
{
  FILE* imagefile;
  dc1394_cameracapture camera;
  int numNodes;
  int numCameras;
  raw1394handle_t handle;
  nodeid_t * camera_nodes;
  int grab_n_frames = 100;
  time_t start_time;
  time_t elapsed_time;
  int i;
  
  

  /*-----------------------------------------------------------------------
   *  Open ohci and asign handle to it
   *-----------------------------------------------------------------------*/
  handle = dc1394_create_handle(0);
  if (handle==NULL)
  {
    fprintf( stderr, "Unable to aquire a raw1394 handle\n"
             "did you insmod the drivers?\n");
    exit(1);
  }

  
  /*-----------------------------------------------------------------------
   *  get the camera nodes and describe them as we find them
   *-----------------------------------------------------------------------*/
  numNodes = raw1394_get_nodecount(handle);
  camera_nodes = dc1394_get_camera_nodes(handle,&numCameras,1);
  fflush(stdout);
  if (numCameras<1)
  {
    fprintf( stderr, "no cameras found :(\n");
    raw1394_destroy_handle(handle);
    exit(1);
  }
  printf("working with the first camera on the bus\n");
  
  /*-----------------------------------------------------------------------
   *  to prevent the iso-transfer bug from raw1394 system, check if
   *  camera is highest node. For details see 
   *  http://linux1394.sourceforge.net/faq.html#DCbusmgmt
   *  and
   *  http://sourceforge.net/tracker/index.php?func=detail&aid=435107&group_id=8157&atid=108157
   *-----------------------------------------------------------------------*/
  if( camera_nodes[0] == numNodes-1)
  {
    fprintf( stderr, "\n"
             "Sorry, your camera is the highest numbered node\n"
             "of the bus, and has therefore become the root node.\n"
             "The root node is responsible for maintaining \n"
             "the timing of isochronous transactions on the IEEE \n"
             "1394 bus.  However, if the root node is not cycle master \n"
             "capable (it doesn't have to be), then isochronous \n"
             "transactions will not work.  The host controller card is \n"
             "cycle master capable, however, most cameras are not.\n"
             "\n"
             "The quick solution is to add the parameter \n"
             "attempt_root=1 when loading the OHCI driver as a \n"
             "module.  So please do (as root):\n"
             "\n"
             "   rmmod ohci1394\n"
             "   insmod ohci1394 attempt_root=1\n"
             "\n"
             "for more information see the FAQ at \n"
             "http://linux1394.sourceforge.net/faq.html#DCbusmgmt\n"
             "\n");
    exit( 1);
  }
  
  /*-----------------------------------------------------------------------
   *  setup capture for format 7
   *-----------------------------------------------------------------------*/
  if( dc1394_setup_format7_capture(handle,camera_nodes[0],
                                   0, /* channel */
                                   MODE_FORMAT7_0, 
                                   SPEED_400,
                                   -1, /* use max speed */
                                   10, 20, /* left, top */
                                   200, 100,  /* width, height */
                                   &camera) != DC1394_SUCCESS)
  {
    fprintf( stderr,"unable to setup camera in format 7 mode 0-\n"
             "check line %d of %s to make sure\n"
             "that the video mode,framerate and format are\n"
             "supported by your camera\n",
             __LINE__,__FILE__);
    dc1394_release_camera(handle,&camera);
    raw1394_destroy_handle(handle);
    exit(1);
  }
 
  /*-----------------------------------------------------------------------
   *  have the camera start sending us data
   *-----------------------------------------------------------------------*/
  if (dc1394_start_iso_transmission(handle,camera.node)
      !=DC1394_SUCCESS) 
  {
    fprintf( stderr, "unable to start camera iso transmission\n");
    dc1394_release_camera(handle,&camera);
    raw1394_destroy_handle(handle);
    exit(1);
  }

  /*-----------------------------------------------------------------------
   *  capture 1000 frames and measure the time for this operation
   *-----------------------------------------------------------------------*/
  start_time = time(NULL);

  for( i = 0; i < grab_n_frames; ++i)
  {
    /*-----------------------------------------------------------------------
     *  capture one frame
     *-----------------------------------------------------------------------*/
    if (dc1394_single_capture(handle,&camera)!=DC1394_SUCCESS) 
    {
      fprintf( stderr, "unable to capture a frame\n");
      dc1394_release_camera(handle,&camera);
      raw1394_destroy_handle(handle);
      exit(1);
    }

    /*---------------------------------------------------------------------
     *  output elapsed time
     *---------------------------------------------------------------------*/
    elapsed_time = time(NULL) - start_time;
    printf( "got frame %d. elapsed time: %d sec ==> %g frames/second\n",
            i, (int)elapsed_time, (float)i / elapsed_time);
  }
  
  /*-----------------------------------------------------------------------
   *  Stop data transmission
   *-----------------------------------------------------------------------*/
  if (dc1394_stop_iso_transmission(handle,camera.node)!=DC1394_SUCCESS) 
  {
    printf("couldn't stop the camera?\n");
  }

  /*-----------------------------------------------------------------------
   *  save last image as Part.pgm
   *-----------------------------------------------------------------------*/
  imagefile=fopen("Part.pgm","w");
    
  fprintf(imagefile,"P5\n%u %u 255\n", camera.frame_width,
          camera.frame_height );
  fwrite((const char *)camera.capture_buffer, 1,
         camera.frame_height*camera.frame_width, imagefile);
  fclose(imagefile);
  printf("wrote: Part.pgm\n");

  

  
  /*-----------------------------------------------------------------------
   *  Close camera
   *-----------------------------------------------------------------------*/
  dc1394_release_camera(handle,&camera);
  raw1394_destroy_handle(handle);
  return 0;
}
