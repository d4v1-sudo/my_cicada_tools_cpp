@echo off
setlocal enabledelayedexpansion

echo Generating pages.h from transcription.txt...

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$txt = Get-Content -Path 'transcription.txt' -Raw -Encoding utf8;" ^
    "$chunks = $txt -split '%%' | Where-Object { $_.Trim() -ne '' };" ^
    "$count = @($chunks).Count + 1;" ^
    "$nl = [char]10;" ^
    "$q = [char]34;" ^
    "$header = '#ifndef PAGES_H' + $nl + '#define PAGES_H' + $nl + $nl + '#include <array>' + $nl + '#include <string_view>' + $nl + '#include <cstddef>' + $nl + $nl + 'namespace core {' + $nl + $nl + 'struct Page {' + $nl + '    size_t index;' + $nl + '    std::string_view name;' + $nl + '    std::string_view content;' + $nl + '};' + $nl + $nl + 'inline constexpr std::array<Page, ' + $count + '> G_PAGES = {{';" ^
    "$i = 1;" ^
    "foreach ($c in $chunks) {" ^
    "    $clean = $c.Trim();" ^
    "    if ($clean) {" ^
    "        if ($i -eq 2) {" ^
    "            $header += $nl + '    {2, ' + $q + 'Page 2' + $q + ', R' + $q + 'raw()raw' + $q + '}, ';" ^
    "            $i = 3;" ^
    "        }" ^
    "        $header += $nl + '    {' + $i + ', ' + $q + 'Page ' + $i + $q + ', R' + $q + 'raw(' + $clean + ')raw' + $q + '}, ';" ^
    "        $i++;" ^
    "    }" ^
    "}" ^
    "$header += $nl + '}};' + $nl + $nl + '} // namespace core' + $nl + $nl + '#endif // PAGES_H';" ^
    "$header | Out-File -FilePath 'pages.h' -Encoding utf8"

if %ERRORLEVEL% EQU 0 (
    echo Success: file pages.h generated.
) else (
    echo Error generating pages.h
)
pause
