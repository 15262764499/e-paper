param(
    [string]$Port = 'COM18',
    [switch]$Program,
    [switch]$Rewrite
)

$ErrorActionPreference = 'Stop'
if ($Rewrite -and -not $Program) { throw '-Rewrite requires -Program.' }
$firmwarePath = Join-Path $PSScriptRoot '../build_bootloader/epaper_bootloader.bin'
$firmware = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $firmwarePath))
$baseAddress = [uint32]0x08000000
if ($firmware.Length -eq 0 -or $firmware.Length -gt 24576 -or ($firmware.Length % 8) -ne 0) {
    throw 'Bootloader must fit in 24 KiB and have an 8-byte-aligned length.'
}
$hash = (Get-FileHash -LiteralPath $firmwarePath -Algorithm SHA256).Hash
Write-Output "FILE: $firmwarePath; bytes=$($firmware.Length); SHA256=$hash"
$sp = [IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::Even,8,[IO.Ports.StopBits]::One)
$sp.Handshake = [IO.Ports.Handshake]::None
$sp.ReadTimeout = 3000
$sp.WriteTimeout = 3000
$script:lastStage = 'open'
$script:completedBlocks = 0
$script:writeAttempted = $false

function Receive-Ack([string]$stage) {
    $script:lastStage = $stage
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $value = $sp.ReadByte()
    $timer.Stop()
    if ($value -ne 0x79) {
        throw ('{0}: expected 79, received {1:X2} after {2} ms' -f $stage,$value,$timer.ElapsedMilliseconds)
    }
    if ($stage.StartsWith('WRITE') -or $stage.StartsWith('ERASE')) {
        Write-Output ('{0}: ACK 79 ({1} ms)' -f $stage,$timer.ElapsedMilliseconds)
    }
}

function Receive-Bytes([int]$count) {
    $buffer = [byte[]]::new($count)
    for ($i = 0; $i -lt $count; $i++) { $buffer[$i] = $sp.ReadByte() }
    return ,$buffer
}

function Send-Address([uint32]$address) {
    $packet = [byte[]](($address -shr 24),(($address -shr 16) -band 255),
        (($address -shr 8) -band 255),($address -band 255),0)
    $packet[4] = $packet[0] -bxor $packet[1] -bxor $packet[2] -bxor $packet[3]
    $sp.Write($packet,0,5)
}

function Read-Memory([uint32]$address,[int]$length) {
    $sp.Write([byte[]](0x11,0xEE),0,2)
    Receive-Ack ('READ {0:X8} command' -f $address)
    Send-Address $address
    Receive-Ack ('READ {0:X8} address' -f $address)
    $n = $length - 1
    $sp.Write([byte[]]($n,($n -bxor 255)),0,2)
    Receive-Ack ('READ {0:X8} length' -f $address)
    $script:lastStage = ('READ {0:X8} data' -f $address)
    return ,(Receive-Bytes $length)
}

try {
    $sp.DtrEnable = $true
    $sp.RtsEnable = $true
    $sp.Open()
    Start-Sleep -Milliseconds 200
    $sp.RtsEnable = $false
    Start-Sleep -Milliseconds 300
    $sp.DiscardInBuffer()
    $sp.Write([byte[]](0x7F),0,1)
    Receive-Ack 'SYNC'
    Write-Output 'SYNC: ACK 79'

    $sp.Write([byte[]](0x02,0xFD),0,2)
    Receive-Ack 'GET ID command'
    $count = $sp.ReadByte() + 1
    if ($count -ne 2) { throw "Unexpected chip ID length $count" }
    $id = Receive-Bytes $count
    Receive-Ack 'GET ID end'
    if ($id[0] -ne 0x04 -or $id[1] -ne 0x60) { throw 'Unexpected chip ID; refusing write.' }
    Write-Output 'CHIP ID: 0460'

    $optionBytes = Read-Memory 0x1FFF7800 8
    $optionWord = [BitConverter]::ToUInt32($optionBytes,0)
    Write-Output ('OPTION WORD: 0x{0:X8}; nBOOT_SEL={1}; nBOOT1={2}; nBOOT0={3}' -f
        $optionWord,(($optionWord -shr 24) -band 1),
        (($optionWord -shr 25) -band 1),(($optionWord -shr 26) -band 1))

    $different = 0
    $nonEmpty = 0
    for ($offset = 0; $offset -lt $firmware.Length; $offset += 256) {
        $length = [Math]::Min(256,$firmware.Length - $offset)
        $data = Read-Memory ($baseAddress + $offset) $length
        for ($i = 0; $i -lt $length; $i++) {
            if ($data[$i] -ne 255) { $nonEmpty++ }
            if ($data[$i] -ne $firmware[$offset + $i]) { $different++ }
        }
    }
    Write-Output "PRECHECK: bytes=$($firmware.Length), nonFF=$nonEmpty, different=$different"
    if ($Program -and ($different -gt 0 -or $Rewrite)) {
        if ($nonEmpty -gt 0 -and -not $Rewrite) {
            throw 'Target contains data. Use -Rewrite only for an authorized bootloader replacement.'
        }
        if ($Rewrite) {
            # Erase only the pages touched by this bootloader. Preserve the
            # remaining bytes of its last page, even if they are not erased.
            $pageCount = [int][Math]::Ceiling($firmware.Length / 2048.0)
            $backup = [byte[]]::new($pageCount * 2048)
            for ($offset = 0; $offset -lt $backup.Length; $offset += 256) {
                $data = Read-Memory ($baseAddress + $offset) 256
                [Array]::Copy($data,0,$backup,$offset,256)
            }
            $backupDir = Join-Path $PSScriptRoot '../tmp/uart-backups'
            [void][IO.Directory]::CreateDirectory($backupDir)
            $backupPath = Join-Path $backupDir ('boot-pages-{0}-{1}.bin' -f
                $Port,(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
            [IO.File]::WriteAllBytes($backupPath,$backup)
            Write-Output "BACKUP: $backupPath; bytes=$($backup.Length)"
            Write-Output ('BACKUP SHA256: ' + (Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash)
            $programImage = [byte[]]$backup.Clone()
            [Array]::Copy($firmware,0,$programImage,0,$firmware.Length)
            $firmware = $programImage

            $script:lastStage = 'ERASE transmit command'
            $sp.Write([byte[]](0x44,0xBB),0,2)
            Receive-Ack 'ERASE command'
            $packet = [byte[]]::new(2 + 2 * $pageCount + 1)
            $packet[0] = 0
            $packet[1] = $pageCount - 1
            for ($page = 0; $page -lt $pageCount; $page++) {
                $packet[2 + 2 * $page] = 0
                $packet[3 + 2 * $page] = $page
            }
            $checksum = 0
            for ($i = 0; $i -lt $packet.Length - 1; $i++) { $checksum = $checksum -bxor $packet[$i] }
            $packet[$packet.Length - 1] = $checksum
            $script:lastStage = "ERASE pages 0..$($pageCount - 1) transmit page list"
            $sp.ReadTimeout = 20000
            $sp.Write($packet,0,$packet.Length)
            Receive-Ack "ERASE pages 0..$($pageCount - 1)"
            $sp.ReadTimeout = 3000
        }
        for ($offset = 0; $offset -lt $firmware.Length; $offset += 256) {
            $length = [Math]::Min(256,$firmware.Length - $offset)
            $address = $baseAddress + $offset
            $script:lastStage = ('WRITE {0:X8} transmit command' -f $address)
            $script:writeAttempted = $true
            $sp.Write([byte[]](0x31,0xCE),0,2)
            Receive-Ack ('WRITE {0:X8} command' -f $address)
            Send-Address $address
            Receive-Ack ('WRITE {0:X8} address' -f $address)
            $packet = [byte[]]::new($length + 2)
            $packet[0] = $length - 1
            [Array]::Copy($firmware,$offset,$packet,1,$length)
            $checksum = 0
            for ($i = 0; $i -le $length; $i++) { $checksum = $checksum -bxor $packet[$i] }
            $packet[$length + 1] = $checksum
            $script:lastStage = ('WRITE {0:X8} transmit data length={1}' -f $address,$length)
            $sp.Write($packet,0,$packet.Length)
            Receive-Ack ('WRITE {0:X8} data length={1}' -f $address,$length)
            $script:completedBlocks++
        }
        Write-Output "WRITE COMPLETE: $($firmware.Length) bytes, $script:completedBlocks blocks"
        for ($offset = 0; $offset -lt $firmware.Length; $offset += 256) {
            $length = [Math]::Min(256,$firmware.Length - $offset)
            $data = Read-Memory ($baseAddress + $offset) $length
            for ($i = 0; $i -lt $length; $i++) {
                if ($data[$i] -ne $firmware[$offset + $i]) {
                    throw ('VERIFY mismatch at {0:X8}' -f ($baseAddress + $offset + $i))
                }
            }
        }
        Write-Output 'VERIFY PASS: every byte matches firmware'
    } elseif ($different -eq 0) {
        Write-Output 'VERIFY PASS: target already matches firmware; no write needed'
    }
} catch {
    Write-Output "FAILED STAGE: $script:lastStage"
    Write-Output "WRITE ATTEMPTED: $script:writeAttempted; ACKED BLOCKS: $script:completedBlocks"
    Write-Output "ERROR: $($_.Exception.Message)"
    throw
} finally {
    if ($sp.IsOpen) {
        $sp.DtrEnable = $false
        $sp.RtsEnable = $false
        $sp.Close()
    }
    $sp.Dispose()
    Write-Output 'PORT RELEASED; no GO or option-byte change sent'
}
