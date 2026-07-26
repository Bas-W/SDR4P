# SDR++++, an SDR++ expansion
![Screenshot](./wiki/UI_Screenshot.png)

## Features
**Features added by this fork:**
* Improved bookmarks
  * Bookmarks displayed on FFT / waterfall plot now stack instead of overlapping
  * Bookmark color can be customized
* VFO
  * The VFO indicator now displays its name
  * The VFO indicator is colored more clearly
  * The VFO bandwidth indicator visibility is improved
* Menu
  * Added menu bar on right side [WIP]
* FFT / waterfall plot
  * Added ability to zoom using CTRL + mouse wheel

**SDR++ features:**
* Multi VFO
* Wide hardware support (both through SoapySDR and dedicated modules)
* SIMD accelerated DSP
* Cross-platform (Windows, Linux, MacOS and BSD)
* Full waterfall update when possible. Makes browsing signals easier and more pleasant
* Modular design (easily write your own plugins)


## Manual
See the [manual](./wiki/manual.md) for information regarding controls and usage of this software. 

## Goal
This fork serves as a place to keep my modifications to SDR++, mostly UI/UX related.

## Credits
[Alexandre Rouma](https://github.com/AlexandreRouma), developer of SDR++.

See [the SDR++ repo](https://github.com/AlexandreRouma/SDRPlusPlus) for a full list of upstream contributors.

### Libraries used
* [ImPlot](https://github.com/epezent/implot)
* [Tracy Profiler](https://github.com/wolfpld/tracy)

### Libraries used upstream

* [SoapySDR (PothosWare)](https://github.com/pothosware/SoapySDR)
* [Dear ImGui (ocornut)](https://github.com/ocornut/imgui)
* [json (nlohmann)](https://github.com/nlohmann/json)
* [rtaudio](http://www.portaudio.com/)
* [Portable File Dialogs](https://github.com/samhocevar/portable-file-dialogs)
