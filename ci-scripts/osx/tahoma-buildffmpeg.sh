cd thirdparty

echo ">>> Cloning ffmpeg"
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg

cd ffmpeg
echo "*" >| .gitignore

echo ">>> Configuring to build ffmpeg (shared)"
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

./configure  --prefix=/usr/local \
      --enable-shared \
      --enable-pthreads \
      --enable-version3 \
      --enable-avresample \
      --enable-gnutls \
      --enable-libaom \
      --enable-libbluray \
      --enable-libdav1d \
      --enable-libmp3lame \
      --enable-libopus \
      --enable-librav1e \
      --enable-libsnappy \
      --enable-libtesseract \
      --enable-libtheora \
      --enable-libvorbis \
      --enable-libvpx \
      --enable-libwebp \
      --enable-libxml2 \
      --enable-lzma \
      --enable-libfontconfig \
      --enable-libfreetype \
      --enable-libass \
      --enable-libopencore-amrnb \
      --enable-libopencore-amrwb \
      --enable-libopenjpeg \
      --enable-libspeex \
      --enable-libsoxr \
      --enable-videotoolbox \
      --enable-libopenh264 \
      --disable-libjack \
      --disable-indev=jack

echo ">>> Building ffmpeg (shared)"
make -j7 # runs 7 jobs in parallel

echo ">>> Installing ffmpeg (shared)"
sudo make install

echo ">>> Configuring to build ffmpeg (static)"
make clean

export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
export SDKROOT=`xcrun --show-sdk-path`
./configure  --prefix=/usr/local \
      --pkg-config-flags="--static" \
      --enable-static \
      --disable-shared \
      --enable-pic \
      --disable-shared \
      --enable-pthreads \
      --enable-version3 \
      --enable-videotoolbox \
      --enable-avresample \
      --enable-libaom \
      --enable-libxml2 \
      --enable-libvpx \
      --disable-ffplay \
      --disable-sdl2 \
      --disable-libjack \
      --disable-indev=jack

echo ">>> Building ffmpeg (static)"
make -j7 # runs 7 jobs in parallel

echo ">>> Installing to tahoma2d/thirdparty/ffmpeg/bin"
if [ ! -d bin ]
then
   mkdir bin
fi

cp ffmpeg ffprobe bin/
