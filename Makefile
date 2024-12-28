all:
	$(CXX) -shared -fPIC --no-gnu-unique src/main.cpp src/overview.cpp src/input.cpp src/render.cpp -o libhyprtasking.so -g `pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon` -std=c++2b -Wno-narrowing
clean:
	rm ./libhyprtasking.so
