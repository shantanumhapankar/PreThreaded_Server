# -*- MakeFile -*-

runserver: server client cv_gray cv_display
	# sudo ./Server/server 8000

server: 
	gcc Server/server.c -pthread -o Server/server

client: 
	gcc Client/client.c -pthread -o Client/client

cv_gray: cv_gray_cmake cv_gray_make

cv_display: cv_display_cmake cv_display_make

cv_gray_cmake: 
	cd Server/; cmake .

cv_gray_make: 
	cd Server/; make

cv_display_cmake: 
	cd Client/; cmake .

cv_display_make:
	cd Client/; make

clean: 
	find -type f \( -executable -o -name "gs_*"  \) -delete
