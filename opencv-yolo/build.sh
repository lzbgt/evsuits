root="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
for app in "$@"
do
    app=${root}/${app}
    cmake --no-warn-unused-cli \
    -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE \
    -H${app} \
    -B${app}/build \
    -G "Visual Studio 16 2019" \
    -T host=x86 -A x64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=install &&\
    cmake --build ${app}/build  --config Release --target ALL_BUILD INSTALL -j 17
done