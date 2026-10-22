"""Run the packaged document renderer with Word's PDF export on Windows.
The packaged LibreOffice executable is unavailable on this Windows runtime.
"""
from pathlib import Path
import importlib.util,subprocess,os,sys
ROOT=Path(__file__).resolve().parent
skill=Path(r'C:\Users\Pterosaur15pro\.codex\plugins\cache\openai-primary-runtime\documents\26.1015.12040\skills\documents')
spec=importlib.util.spec_from_file_location('packaged_renderer',skill/'render_docx.py')
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
def convert(doc_path,user_profile,convert_tmp_dir,stem,verbose):
    pdf=Path(convert_tmp_dir)/(stem+'.pdf')
    result=subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'word_export_pdf.ps1'),'-DocPath',doc_path,'-PdfPath',str(pdf)],capture_output=True,text=True,timeout=120)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return str(pdf),'Microsoft Word PDF export'
renderer.convert_to_pdf=convert
os.environ['PATH']=r'C:\Users\Pterosaur15pro\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\poppler\Library\bin'+os.pathsep+os.environ['PATH']
sys.argv=['render_docx.py',str(ROOT.parent/'感应加热HMI_V6调试与STM32接口手册.docx'),'--output_dir',str(ROOT/'word_qa'),'--emit_pdf']
renderer.main()
