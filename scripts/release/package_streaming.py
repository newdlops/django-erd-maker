"""Write the staged VSIX using the same metadata as the prior vsce package.

ZIP member bytes are streamed, retaining executable permissions. Stage and
unpacked-runtime verification remain in package-release.mjs. No signing,
dependency installation or publishing occurs here.
"""
import argparse
import json
import mimetypes
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

VSX='http://schemas.microsoft.com/developer/vsx-schema/2011'
TYPES='http://schemas.openxmlformats.org/package/2006/content-types'

def package(stage,output):
    manifest=json.loads((stage/'package.json').read_text())
    ET.register_namespace('',VSX)
    def element(parent,tag,text=None,**attributes):
        node=ET.SubElement(parent,'{'+VSX+'}'+tag,attributes)
        if text is not None:node.text=text
        return node
    root=ET.Element('{'+VSX+'}PackageManifest',Version='2.0.0')
    metadata=element(root,'Metadata')
    element(metadata,'Identity',Language='en-US',Id=manifest['name'],Version=manifest['version'],
        Publisher=manifest['publisher'],TargetPlatform='darwin-arm64')
    element(metadata,'DisplayName',manifest.get('displayName',manifest['name']))
    description=element(metadata,'Description',manifest.get('description',''))
    description.set('{http://www.w3.org/XML/1998/namespace}space','preserve')
    element(metadata,'Tags',','.join(manifest.get('keywords',[])))
    element(metadata,'Categories',','.join(manifest.get('categories',[])))
    element(metadata,'GalleryFlags','Public Preview' if manifest.get('preview') else 'Public')
    properties=element(metadata,'Properties')
    repository=manifest.get('repository',{})
    url=repository.get('url','') if isinstance(repository,dict) else repository
    values={'Microsoft.VisualStudio.Code.Engine':manifest['engines']['vscode'],
        'Microsoft.VisualStudio.Code.ExtensionDependencies':','.join(manifest.get('extensionDependencies',[])),
        'Microsoft.VisualStudio.Code.ExtensionPack':','.join(manifest.get('extensionPack',[])),
        'Microsoft.VisualStudio.Code.ExtensionKind':','.join(manifest.get('extensionKind',[])),
        'Microsoft.VisualStudio.Code.LocalizedLanguages':'','Microsoft.VisualStudio.Code.EnabledApiProposals':'',
        'Microsoft.VisualStudio.Code.PreRelease':'true','Microsoft.VisualStudio.Code.ExecutesCode':'true',
        'Microsoft.VisualStudio.Services.Links.Source':url,'Microsoft.VisualStudio.Services.Links.Getstarted':url,
        'Microsoft.VisualStudio.Services.Links.GitHub':url,
        'Microsoft.VisualStudio.Services.Links.Support':manifest.get('bugs',{}).get('url',''),
        'Microsoft.VisualStudio.Services.Links.Learn':manifest.get('homepage',''),
        'Microsoft.VisualStudio.Services.Branding.Color':manifest.get('galleryBanner',{}).get('color',''),
        'Microsoft.VisualStudio.Services.Branding.Theme':manifest.get('galleryBanner',{}).get('theme',''),
        'Microsoft.VisualStudio.Services.GitHubFlavoredMarkdown':'true',
        'Microsoft.VisualStudio.Services.Content.Pricing':'Free'}
    for key,value in values.items():element(properties,'Property',Id=key,Value=value)
    element(metadata,'License','extension/LICENSE.txt')
    element(metadata,'Icon','extension/'+manifest['icon'])
    installation=element(root,'Installation');element(installation,'InstallationTarget',Id='Microsoft.VisualStudio.Code')
    element(root,'Dependencies')
    assets=element(root,'Assets')
    for kind,name in [('Microsoft.VisualStudio.Code.Manifest','package.json'),
        ('Microsoft.VisualStudio.Services.Content.Details','readme.md'),
        ('Microsoft.VisualStudio.Services.Content.Changelog','changelog.md'),
        ('Microsoft.VisualStudio.Services.Content.License','LICENSE.txt'),
        ('Microsoft.VisualStudio.Services.Icons.Default',manifest['icon'])]:
        element(assets,'Asset',Type=kind,Path='extension/'+name,Addressable='true')
    xml=ET.tostring(root,encoding='utf-8',xml_declaration=True)
    rename={'README.md':'readme.md','README.ko.md':'readme.ko.md','CHANGELOG.md':'changelog.md','LICENSE':'LICENSE.txt'}
    files=[]
    for file in sorted(stage.rglob('*')):
        if not file.is_file() or file==output or file.name=='.vscodeignore':continue
        assert not file.is_symlink(),file
        relative=file.relative_to(stage).as_posix()
        files.append((file,'extension/'+rename.get(relative,relative)))
    ET.register_namespace('',TYPES)
    types=ET.Element('{'+TYPES+'}Types')
    suffixes={Path(name).suffix.lower() for _,name in files}|{'.vsixmanifest'}
    for suffix in sorted(suffixes- {''}):
        mime={'.js':'application/javascript','.mjs':'application/javascript','.md':'text/markdown',
            '.json':'application/json','.npz':'application/octet-stream','.gz':'application/gzip',
            '.vsixmanifest':'text/xml'}.get(suffix,mimetypes.guess_type('file'+suffix)[0] or 'application/octet-stream')
        ET.SubElement(types,'{'+TYPES+'}Default',Extension=suffix,ContentType=mime)
    with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as package:
        package.writestr('extension.vsixmanifest',xml)
        package.writestr('[Content_Types].xml',ET.tostring(types,encoding='utf-8',xml_declaration=True))
        for file,name in files:
            compression=zipfile.ZIP_STORED if file.suffix.lower() in ('.gz','.npz','.png') else zipfile.ZIP_DEFLATED
            package.write(file,name,compress_type=compression)
    print(json.dumps(dict(version=manifest['version'],files=len(files),bytes=output.stat().st_size,
        packaging='streamed-standard-vsix',target='darwin-arm64')))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
    package(args.stage.resolve(),args.out.resolve())
