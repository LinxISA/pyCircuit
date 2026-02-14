#!/usr/bin/env python3
"""Convert CUBE_V2_SPEC.md to PDF using reportlab."""

import os
import re
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import cm
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Preformatted, Table, TableStyle, PageBreak
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT, TA_CENTER

# Read the markdown file
script_dir = os.path.dirname(os.path.abspath(__file__))
md_path = os.path.join(script_dir, "CUBE_V2_SPEC.md")
pdf_path = os.path.join(script_dir, "CUBE_V2_SPEC.pdf")

with open(md_path, "r", encoding="utf-8") as f:
    md_content = f.read()

# Create PDF document
doc = SimpleDocTemplate(
    pdf_path,
    pagesize=A4,
    rightMargin=1.5*cm,
    leftMargin=1.5*cm,
    topMargin=1.5*cm,
    bottomMargin=1.5*cm
)

# Get styles
styles = getSampleStyleSheet()

# Custom styles
styles.add(ParagraphStyle(
    name='Title1',
    parent=styles['Heading1'],
    fontSize=20,
    spaceAfter=12,
    spaceBefore=20,
    textColor=colors.HexColor('#1a1a1a'),
    borderWidth=1,
    borderColor=colors.HexColor('#333333'),
    borderPadding=5,
))

styles.add(ParagraphStyle(
    name='Title2',
    parent=styles['Heading2'],
    fontSize=16,
    spaceAfter=10,
    spaceBefore=16,
    textColor=colors.HexColor('#2a2a2a'),
))

styles.add(ParagraphStyle(
    name='Title3',
    parent=styles['Heading3'],
    fontSize=13,
    spaceAfter=8,
    spaceBefore=12,
    textColor=colors.HexColor('#3a3a3a'),
))

styles.add(ParagraphStyle(
    name='CodeBlock',
    parent=styles['Code'],
    fontSize=7,
    leading=9,
    fontName='Courier',
    backColor=colors.HexColor('#f5f5f5'),
    borderWidth=1,
    borderColor=colors.HexColor('#dddddd'),
    borderPadding=8,
    leftIndent=0,
    rightIndent=0,
))

styles.add(ParagraphStyle(
    name='BodyText2',
    parent=styles['BodyText'],
    fontSize=10,
    leading=14,
    spaceAfter=8,
))

styles.add(ParagraphStyle(
    name='Quote',
    parent=styles['BodyText'],
    fontSize=10,
    leading=14,
    leftIndent=20,
    textColor=colors.HexColor('#555555'),
    fontName='Helvetica-Oblique',
))

# Parse markdown and build story
story = []

def escape_xml(text):
    """Escape XML special characters."""
    text = text.replace('&', '&amp;')
    text = text.replace('<', '&lt;')
    text = text.replace('>', '&gt;')
    return text

def process_inline(text):
    """Process inline markdown (bold, italic, code)."""
    # Escape XML first
    text = escape_xml(text)
    # Bold
    text = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', text)
    # Italic
    text = re.sub(r'\*(.+?)\*', r'<i>\1</i>', text)
    # Inline code
    text = re.sub(r'`(.+?)`', r'<font face="Courier" size="9">\1</font>', text)
    return text

lines = md_content.split('\n')
i = 0
in_code_block = False
code_block_content = []

while i < len(lines):
    line = lines[i]

    # Code block
    if line.startswith('```'):
        if in_code_block:
            # End code block
            code_text = '\n'.join(code_block_content)
            # Use Preformatted for code blocks to preserve spacing
            story.append(Spacer(1, 6))
            story.append(Preformatted(code_text, styles['CodeBlock']))
            story.append(Spacer(1, 6))
            code_block_content = []
            in_code_block = False
        else:
            # Start code block
            in_code_block = True
        i += 1
        continue

    if in_code_block:
        code_block_content.append(line)
        i += 1
        continue

    # Headers
    if line.startswith('# '):
        story.append(Paragraph(process_inline(line[2:]), styles['Title1']))
        i += 1
        continue

    if line.startswith('## '):
        story.append(Paragraph(process_inline(line[3:]), styles['Title2']))
        i += 1
        continue

    if line.startswith('### '):
        story.append(Paragraph(process_inline(line[4:]), styles['Title3']))
        i += 1
        continue

    # Blockquote
    if line.startswith('> '):
        story.append(Paragraph(process_inline(line[2:]), styles['Quote']))
        i += 1
        continue

    # Horizontal rule
    if line.strip() == '---':
        story.append(Spacer(1, 10))
        i += 1
        continue

    # Table
    if '|' in line and i + 1 < len(lines) and '---' in lines[i + 1]:
        # Parse table
        table_data = []
        while i < len(lines) and '|' in lines[i]:
            row = [cell.strip() for cell in lines[i].split('|')[1:-1]]
            if '---' not in lines[i]:
                table_data.append([process_inline(cell) for cell in row])
            i += 1

        if table_data:
            # Create table
            t = Table(table_data, repeatRows=1)
            t.setStyle(TableStyle([
                ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#f0f0f0')),
                ('TEXTCOLOR', (0, 0), (-1, 0), colors.black),
                ('ALIGN', (0, 0), (-1, -1), 'LEFT'),
                ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
                ('FONTSIZE', (0, 0), (-1, -1), 9),
                ('BOTTOMPADDING', (0, 0), (-1, 0), 8),
                ('TOPPADDING', (0, 0), (-1, -1), 6),
                ('BOTTOMPADDING', (0, 1), (-1, -1), 6),
                ('BACKGROUND', (0, 1), (-1, -1), colors.white),
                ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#f9f9f9')]),
                ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor('#dddddd')),
            ]))
            story.append(Spacer(1, 8))
            story.append(t)
            story.append(Spacer(1, 8))
        continue

    # List items
    if line.strip().startswith('- ') or line.strip().startswith('* '):
        bullet_text = line.strip()[2:]
        story.append(Paragraph('• ' + process_inline(bullet_text), styles['BodyText2']))
        i += 1
        continue

    # Numbered list
    match = re.match(r'^(\d+)\.\s+(.+)$', line.strip())
    if match:
        num, text = match.groups()
        story.append(Paragraph(f'{num}. ' + process_inline(text), styles['BodyText2']))
        i += 1
        continue

    # Empty line
    if not line.strip():
        story.append(Spacer(1, 6))
        i += 1
        continue

    # Regular paragraph
    story.append(Paragraph(process_inline(line), styles['BodyText2']))
    i += 1

# Build PDF
doc.build(story)

print(f"PDF generated: {pdf_path}")
