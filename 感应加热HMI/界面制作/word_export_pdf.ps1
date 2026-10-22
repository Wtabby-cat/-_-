param([string]$DocPath, [string]$PdfPath)
$ErrorActionPreference = 'Stop'
$taskWord = $null
$taskDoc = $null
try {
    $taskWord = New-Object -ComObject Word.Application
    $taskWord.Visible = $false
    $taskWord.DisplayAlerts = 0
    $taskDoc = $taskWord.Documents.Open([ref]$DocPath, [ref]$false, [ref]$true)
    $taskDoc.ExportAsFixedFormat($PdfPath, 17)
} finally {
    if ($null -ne $taskDoc) {
        $taskDoc.Close([ref]0)
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($taskDoc)
    }
    if ($null -ne $taskWord) {
        $taskWord.Quit([ref]0)
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($taskWord)
    }
}
