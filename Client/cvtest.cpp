#include <opencv2/opencv.hpp>

using namespace cv;

int main( int argc, char** argv )
{
 char* imageName = argv[1];
 char gray[100] = "";
 Mat image, grayimage;
 strcat(gray, "gs_");
 strcat(gray, imageName);
 image = imread( imageName, 1 );
 grayimage = imread(gray, 1);


 if( argc != 2 || !image.data )
 {
   printf( " No image data \n " );
   return -1;
 }

 // Mat gray_image;
 // cvtColor( image, gray_image, COLOR_BGR2GRAY );

 // imwrite(imageName, gray_image );

 // // namedWindow( imageName, WINDOW_AUTOSIZE );
 // // namedWindow( "Gray image", WINDOW_AUTOSIZE );

 imshow( imageName, image );
 imshow(gray, grayimage);
 // imshow( "Gray image", gray_image );

 waitKey(0);

 return 0;
}