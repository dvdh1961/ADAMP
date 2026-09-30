#include "ataridebuggerwindow.h"
#include "colecocontroller.h"
#include <algorithm>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QClipboard>
#include <QColor>
#include <QDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QGuiApplication>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVariantList>

namespace {
QString hx(unsigned v,int w){return QString("%1").arg(v,w,16,QChar('0')).toUpper();}
QTableWidgetItem* cell(const QString& text,const QColor& color=Qt::white){auto* i=new QTableWidgetItem(text);i->setForeground(color);i->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);return i;}
struct OpcodeInfo{QString name,mode;int bytes=1;};
OpcodeInfo opcodeInfo(quint8 op){
 static const QStringList names=QString(
 "BRK ORA JAM SLO NOP ORA ASL SLO PHP ORA ASL ANC NOP ORA ASL SLO "
 "BPL ORA JAM SLO NOP ORA ASL SLO CLC ORA NOP SLO NOP ORA ASL SLO "
 "JSR AND JAM RLA BIT AND ROL RLA PLP AND ROL ANC BIT AND ROL RLA "
 "BMI AND JAM RLA NOP AND ROL RLA SEC AND NOP RLA NOP AND ROL RLA "
 "RTI EOR JAM SRE NOP EOR LSR SRE PHA EOR LSR ALR JMP EOR LSR SRE "
 "BVC EOR JAM SRE NOP EOR LSR SRE CLI EOR NOP SRE NOP EOR LSR SRE "
 "RTS ADC JAM RRA NOP ADC ROR RRA PLA ADC ROR ARR JMP ADC ROR RRA "
 "BVS ADC JAM RRA NOP ADC ROR RRA SEI ADC NOP RRA NOP ADC ROR RRA "
 "NOP STA NOP SAX STY STA STX SAX DEY NOP TXA XAA STY STA STX SAX "
 "BCC STA JAM AHX STY STA STX SAX TYA STA TXS TAS SHY STA SHX AHX "
 "LDY LDA LDX LAX LDY LDA LDX LAX TAY LDA TAX LAX LDY LDA LDX LAX "
 "BCS LDA JAM LAX LDY LDA LDX LAX CLV LDA TSX LAS LDY LDA LDX LAX "
 "CPY CMP NOP DCP CPY CMP DEC DCP INY CMP DEX AXS CPY CMP DEC DCP "
 "BNE CMP JAM DCP NOP CMP DEC DCP CLD CMP NOP DCP NOP CMP DEC DCP "
 "CPX SBC NOP ISC CPX SBC INC ISC INX SBC NOP SBC CPX SBC INC ISC "
 "BEQ SBC JAM ISC NOP SBC INC ISC SED SBC NOP ISC NOP SBC INC ISC").split(' ');
 static const QStringList modes=QString(
 "IMP IZX IMP IZX ZP ZP ZP ZP IMP IMM ACC IMM ABS ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
 "ABS IZX IMP IZX ZP ZP ZP ZP IMP IMM ACC IMM ABS ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
 "IMP IZX IMP IZX ZP ZP ZP ZP IMP IMM ACC IMM ABS ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
 "IMP IZX IMP IZX ZP ZP ZP ZP IMP IMM ACC IMM IND ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
 "IMM IZX IMM IZX ZP ZP ZP ZP IMP IMM IMP IMM ABS ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPY ZPY IMP ABY IMP ABY ABX ABX ABY ABY "
 "IMM IZX IMM IZX ZP ZP ZP ZP IMP IMM IMP IMM ABS ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPY ZPY IMP ABY IMP ABY ABX ABX ABY ABY "
 "IMM IZX IMM IZX ZP ZP ZP ZP IMP IMM IMP IMM ABS ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX "
 "IMM IZX IMM IZX ZP ZP ZP ZP IMP IMM IMP IMM ABS ABS ABS ABS "
 "REL IZY IMP IZY ZPX ZPX ZPX ZPX IMP ABY IMP ABY ABX ABX ABX ABX").split(' ');
 OpcodeInfo i{names[op],modes[op],1};if(i.mode=="IMM"||i.mode=="ZP"||i.mode=="ZPX"||i.mode=="ZPY"||i.mode=="IZX"||i.mode=="IZY"||i.mode=="REL")i.bytes=2;else if(i.mode=="ABS"||i.mode=="ABX"||i.mode=="ABY"||i.mode=="IND")i.bytes=3;return i;
}
int instructionLength(quint8 op){return opcodeInfo(op).bytes;}
QString disassemble(quint8 op,quint8 lo,quint8 hi,quint16 address,QString* target){const OpcodeInfo i=opcodeInfo(op);const quint16 word=quint16(lo)|(quint16(hi)<<8);QString operand;if(i.mode=="ACC")operand="A";else if(i.mode=="IMM")operand="#$"+hx(lo,2);else if(i.mode=="ZP")operand="$"+hx(lo,2);else if(i.mode=="ZPX")operand="$"+hx(lo,2)+",X";else if(i.mode=="ZPY")operand="$"+hx(lo,2)+",Y";else if(i.mode=="IZX")operand="($"+hx(lo,2)+",X)";else if(i.mode=="IZY")operand="($"+hx(lo,2)+"),Y";else if(i.mode=="ABS")operand="$"+hx(word,4);else if(i.mode=="ABX")operand="$"+hx(word,4)+",X";else if(i.mode=="ABY")operand="$"+hx(word,4)+",Y";else if(i.mode=="IND")operand="($"+hx(word,4)+")";else if(i.mode=="REL"){const quint16 branch=quint16(address+2+qint8(lo));operand="$"+hx(branch,4);if(target)*target=operand;}if(target&&target->isEmpty()&&(i.name=="JMP"||i.name=="JSR"))*target="$"+hx(word,4);return operand.isEmpty()?i.name:i.name+" "+operand;}
}

AtariDebuggerWindow::AtariDebuggerWindow(QWidget* parent):QMainWindow(parent){
 setWindowTitle("ADAM+ Atari 2600 Debugger");setMinimumSize(1080,800);resize(1080,800);auto* central=new QWidget(this);setCentralWidget(central);auto* root=new QVBoxLayout(central);QFont tf("Roboto",10),rf("Roboto",11),fixed=QFontDatabase::systemFont(QFontDatabase::FixedFont);
 auto setup=[&](QTableWidget* t){t->verticalHeader()->hide();t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setFont(tf);};auto* top=new QHBoxLayout;
 m_programTabs=new QTabWidget;auto* pw=new QWidget;auto* pl=new QVBoxLayout(pw);pl->setContentsMargins(4,4,4,4);m_disassembly=new QTableWidget(0,5);setup(m_disassembly);m_disassembly->setHorizontalHeaderLabels({"Label","Addr","Code","Mnemonic","Target"});m_disassembly->setColumnWidth(0,100);m_disassembly->setColumnWidth(1,55);m_disassembly->setColumnWidth(2,90);m_disassembly->setColumnWidth(3,100);m_disassembly->horizontalHeader()->setStretchLastSection(true);pl->addWidget(m_disassembly);m_programTabs->addTab(pw,QIcon(":/images/images/LED_GRAY.png"),"Program");top->addWidget(m_programTabs,1);
 auto* rt=new QTabWidget;rt->setFixedWidth(140);auto* rw=new QWidget;auto* rl=new QVBoxLayout(rw);rl->setContentsMargins(4,4,4,4);m_registers=new QTableWidget(0,2);setup(m_registers);m_registers->setFont(rf);m_registers->setHorizontalHeaderLabels({"Reg","Value"});m_registers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);rl->addWidget(m_registers);rt->addTab(rw,"6507");top->addWidget(rt);
 auto* right=new QVBoxLayout;auto* ft=new QTabWidget;ft->setFixedWidth(320);auto* fw=new QWidget;auto* fl=new QVBoxLayout(fw);fl->setContentsMargins(4,4,4,4);m_flags=new QTableWidget(1,8);setup(m_flags);m_flags->setHorizontalHeaderLabels({"N","V","-","B","D","I","Z","C"});m_flags->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);m_flags->setMaximumHeight(70);fl->addWidget(m_flags);ft->addTab(fw,"Flags");right->addWidget(ft);auto* ht=new QTabWidget;ht->setFixedWidth(320);m_tiaRiot=new QTableWidget(0,2);setup(m_tiaRiot);m_tiaRiot->setHorizontalHeaderLabels({"TIA / RIOT","Value"});m_tiaRiot->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);ht->addTab(m_tiaRiot,"TIA / RIOT");right->addWidget(ht,1);top->addLayout(right);root->addLayout(top,1);
 auto* bottom=new QHBoxLayout;auto* mt=new QTabWidget;auto* mw=new QWidget;auto* ml=new QVBoxLayout(mw);ml->setContentsMargins(4,4,4,4);m_memory=new QTableWidget(0,18);setup(m_memory);QStringList mh{""};for(int i=0;i<16;++i)mh<<hx(i,2);mh<<"ASCII";m_memory->setHorizontalHeaderLabels(mh);m_memory->setFont(fixed);m_memory->setColumnWidth(0,48);for(int i=1;i<=16;++i)m_memory->setColumnWidth(i,28);m_memory->horizontalHeader()->setStretchLastSection(true);ml->addWidget(m_memory);auto* memoryControls=new QHBoxLayout;memoryControls->addStretch();memoryControls->addWidget(new QLabel("Address:"));auto* memPrev=new QPushButton("<");memPrev->setFixedSize(24,24);memPrev->setFont(fixed);memoryControls->addWidget(memPrev);m_memoryAddressEdit=new QLineEdit("0000");m_memoryAddressEdit->setFont(fixed);m_memoryAddressEdit->setFixedWidth(60);m_memoryAddressEdit->setMaxLength(4);m_memoryAddressEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-1]?[0-9a-fA-F]{1,3}"),this));memoryControls->addWidget(m_memoryAddressEdit);auto* memNext=new QPushButton(">");memNext->setFixedSize(24,24);memNext->setFont(fixed);memoryControls->addWidget(memNext);auto* memHome=new QPushButton("-0-");memHome->setFixedSize(24,24);memHome->setFont(fixed);memoryControls->addWidget(memHome);memoryControls->addSpacing(12);auto* copyRange=new QPushButton("Copy range");copyRange->setFixedHeight(24);memoryControls->addWidget(copyRange);ml->addLayout(memoryControls);mt->addTab(mw,"Memory");bottom->addWidget(mt,1);
 auto* bt=new QTabWidget;bt->setFixedWidth(320);auto* bw=new QWidget;auto* bl=new QVBoxLayout(bw);bl->setContentsMargins(4,4,4,4);m_breakpointList=new QListWidget;m_breakpointList->setFont(fixed);bl->addWidget(m_breakpointList);auto* be=new QHBoxLayout;m_breakpointEdit=new QLineEdit;m_breakpointEdit->setPlaceholderText("F000");m_breakpointEdit->setFixedWidth(75);auto* add=new QPushButton("Add");auto* del=new QPushButton("Delete");be->addWidget(m_breakpointEdit);be->addWidget(add);be->addWidget(del);bl->addLayout(be);bt->addTab(bw,"Breakpoints");bottom->addWidget(bt);root->addLayout(bottom,1);
 auto ib=[&](const QString& path){auto* b=new QPushButton;QPixmap p(path);b->setIcon(QIcon(path));b->setIconSize(p.size());b->setFixedSize(p.size());b->setFlat(true);b->setStyleSheet("QPushButton{border:none;background:transparent} QPushButton:pressed{padding:2px 0 0 2px}");return b;};auto* controls=new QHBoxLayout;auto* step=ib(":/images/images/SSTEP.png");auto* run=ib(":/images/images/RUN.png");auto* pause=ib(":/images/images/BREAK.png");controls->addWidget(step);controls->addWidget(run);controls->addStretch();m_status=new QLabel("STOP");controls->addWidget(m_status);controls->addWidget(pause);root->addLayout(controls);
 connect(pause,&QPushButton::clicked,this,[this]{m_instructionRun=false;if(m_controller)QMetaObject::invokeMethod(m_controller,"pauseAtariDebugger",Qt::QueuedConnection);});
 connect(run,&QPushButton::clicked,this,[this]{m_instructionRun=true;if(m_controller)QMetaObject::invokeMethod(m_controller,"pauseAtariDebugger",Qt::QueuedConnection);});
 connect(step,&QPushButton::clicked,this,[this]{m_instructionRun=false;if(m_controller)QMetaObject::invokeMethod(m_controller,"stepAtariDebugger",Qt::QueuedConnection);});
 connect(add,&QPushButton::clicked,this,&AtariDebuggerWindow::addBreakpoint);connect(del,&QPushButton::clicked,this,&AtariDebuggerWindow::removeBreakpoint);connect(m_breakpointEdit,&QLineEdit::returnPressed,this,&AtariDebuggerWindow::addBreakpoint);
 connect(m_memoryAddressEdit,&QLineEdit::returnPressed,this,&AtariDebuggerWindow::goToMemoryAddress);connect(memPrev,&QPushButton::clicked,this,&AtariDebuggerWindow::previousMemoryLine);connect(memNext,&QPushButton::clicked,this,&AtariDebuggerWindow::nextMemoryLine);connect(memHome,&QPushButton::clicked,this,&AtariDebuggerWindow::homeMemory);connect(copyRange,&QPushButton::clicked,this,&AtariDebuggerWindow::copyMemoryRange);
 m_liveRefreshTimer=new QTimer(this);m_liveRefreshTimer->setInterval(16);
 connect(m_liveRefreshTimer,&QTimer::timeout,this,[this]{
     if(!isVisible()||!m_controller)return;
     QMetaObject::invokeMethod(m_controller,m_instructionRun?"runAtariDebuggerBurst":"requestAtariDebuggerState",Qt::QueuedConnection);
 });
 m_liveRefreshTimer->start();
}
void AtariDebuggerWindow::setController(ColecoController* c){m_controller=c;connect(c,&ColecoController::atariDebuggerStateChanged,this,&AtariDebuggerWindow::updateState,Qt::QueuedConnection);}
void AtariDebuggerWindow::refresh(){if(m_controller)QMetaObject::invokeMethod(m_controller,"requestAtariDebuggerState",Qt::QueuedConnection);}
void AtariDebuggerWindow::updateState(const QVariantMap& s){
 if(!s.value("hasRom").toBool()){m_disassemblyBaseValid=false;m_traceCursor=0;m_status->setText("STOP - No Atari cartridge");m_disassembly->setRowCount(0);m_memory->setRowCount(0);return;}bool paused=s.value("paused").toBool();m_status->setText(paused?"PAUSE":"RUN TRACE");m_status->setStyleSheet(paused?"color:#ffcc00;font-weight:bold":"color:#55dd55;font-weight:bold");m_programTabs->setTabIcon(0,QIcon(paused?":/images/images/LED_YELLOW.png":":/images/images/LED_GREEN.png"));const QVariantList trace=s.value("traceEntries").toList();QVariantMap cpuState=s;if(!paused&&!trace.isEmpty()){m_traceCursor=(m_traceCursor+1)%trace.size();cpuState=trace[m_traceCursor].toMap();}const unsigned pc=cpuState.value("pc").toUInt(),p=cpuState.value("p").toUInt();QStringList rn={"PC","SP","A","X","Y"};QList<unsigned> rv={pc,cpuState.value("sp").toUInt(),cpuState.value("a").toUInt(),cpuState.value("x").toUInt(),cpuState.value("y").toUInt()};m_registers->setRowCount(rn.size());for(int i=0;i<rn.size();++i){m_registers->setItem(i,0,cell(rn[i],i==0?QColor("#4FC3F7"):QColor("#9E9E9E")));m_registers->setItem(i,1,cell("$"+hx(rv[i],i==0?4:2),i==0?QColor("#4FC3F7"):Qt::white));}for(int i=0;i<8;++i)m_flags->setItem(0,i,cell((p&(0x80>>i))?"1":"0",(p&(0x80>>i))?QColor("#4FC3F7"):QColor("#777777")));
 if(m_instructionRun&&!trace.isEmpty()){
     m_traceCursor=(m_traceCursor+1)%trace.size();
     const QVariantMap traced=trace[m_traceCursor].toMap();
     const unsigned tracedP=traced.value("p").toUInt();
     const QList<unsigned> tracedRegisters={traced.value("pc").toUInt(),traced.value("sp").toUInt(),traced.value("a").toUInt(),traced.value("x").toUInt(),traced.value("y").toUInt()};
     for(int i=0;i<tracedRegisters.size();++i)m_registers->setItem(i,1,cell("$"+hx(tracedRegisters[i],i==0?4:2),i==0?QColor("#4FC3F7"):Qt::white));
     for(int i=0;i<8;++i)m_flags->setItem(0,i,cell((tracedP&(0x80>>i))?"1":"0",(tracedP&(0x80>>i))?QColor("#4FC3F7"):QColor("#777777")));
 }
 if(m_instructionRun){m_status->setText("RUN FRAME");m_status->setStyleSheet("color:#55dd55;font-weight:bold");m_programTabs->setTabIcon(0,QIcon(":/images/images/LED_GREEN.png"));}
 QStringList hn={"Scanline","Color clock","SWCHA","SWCHB","INTIM","INSTAT","Mapper","Bank"},hv={QString::number(s.value("scanline").toInt()),QString::number(s.value("colorClock").toInt()),"$"+hx(s.value("swcha").toUInt(),2),"$"+hx(s.value("swchb").toUInt(),2),"$"+hx(s.value("intim").toUInt(),2),"$"+hx(s.value("instat").toUInt(),2),s.value("mapper").toString(),QString::number(s.value("bank").toInt())};m_tiaRiot->setRowCount(hn.size());for(int i=0;i<hn.size();++i){m_tiaRiot->setItem(i,0,cell(hn[i],QColor("#9E9E9E")));m_tiaRiot->setItem(i,1,cell(hv[i]));}
 QByteArray program=s.value("program").toByteArray();const quint16 programBase=quint16(s.value("programBase").toUInt());
 auto addDecodedRow=[&](int row,quint16 address,bool current){const int index=int(quint16(address-programBase));if(index<0||index>=program.size())return false;const quint8 op=quint8(program[index]);const int len=qMin(instructionLength(op),program.size()-index);QString raw;for(int i=0;i<len;++i)raw+=hx(quint8(program[index+i]),2)+(i+1<len?" ":"");QString target;const quint8 lo=index+1<program.size()?quint8(program[index+1]):0,hi=index+2<program.size()?quint8(program[index+2]):0;const QString instruction=disassemble(op,lo,hi,address,&target);m_disassembly->insertRow(row);m_disassembly->setItem(row,0,cell(current?">":"",QColor("#FFA500")));m_disassembly->setItem(row,1,cell(hx(address,4),QColor("#4FC3F7")));m_disassembly->setItem(row,2,cell(raw,QColor("#81C784")));m_disassembly->setItem(row,3,cell(instruction,QColor("#E0E0E0")));m_disassembly->setItem(row,4,cell(target,QColor("#FFA500")));if(current){for(int column=0;column<m_disassembly->columnCount();++column){auto* item=m_disassembly->item(row,column);item->setBackground(QColor(200,30,30));item->setForeground(Qt::white);}}return true;};
 auto addTraceRow=[&](int row,const QVariantMap& entry,bool current){const quint16 address=quint16(entry.value("pc").toUInt());const quint8 op=quint8(entry.value("op").toUInt()),lo=quint8(entry.value("b1").toUInt()),hi=quint8(entry.value("b2").toUInt());const int len=instructionLength(op);QString raw=hx(op,2);if(len>1)raw+=" "+hx(lo,2);if(len>2)raw+=" "+hx(hi,2);QString target;const QString instruction=disassemble(op,lo,hi,address,&target);m_disassembly->insertRow(row);m_disassembly->setItem(row,0,cell(current?">":"",QColor("#FFA500")));m_disassembly->setItem(row,1,cell(hx(address,4),QColor("#4FC3F7")));m_disassembly->setItem(row,2,cell(raw,QColor("#81C784")));m_disassembly->setItem(row,3,cell(instruction,QColor("#E0E0E0")));m_disassembly->setItem(row,4,cell(target,QColor("#FFA500")));if(current){for(int column=0;column<m_disassembly->columnCount();++column){auto* item=m_disassembly->item(row,column);item->setBackground(QColor(200,30,30));item->setForeground(Qt::white);}}};
 m_disassembly->setRowCount(0);
 if((m_instructionRun||!paused)&&!trace.isEmpty()){
     // Zelfde presentatie als de ADAM-debugger: 64 regels, de uitvoeringsrij
     // vast in het midden en de werkelijk uitgevoerde code schuift erachterdoor.
     const int count=trace.size(),visible=qMin(64,count),center=visible/2;
     const int first=(m_traceCursor-center+count)%count;
     for(int row=0;row<visible;++row){const int traceIndex=(first+row)%count;addTraceRow(row,trace[traceIndex].toMap(),row==center);}
     if(m_disassembly->item(center,0))m_disassembly->scrollToItem(m_disassembly->item(center,0),QAbstractItemView::PositionAtCenter);
 }else{
     // Ook in pauze/single-step blijft PC op de centrale rij. De 32 werkelijk
     // voorafgaande instructies komen uit de trace; daarna volgt lineaire code.
     constexpr int center=32;const int before=qMin(center,trace.size());
     for(int pad=0;pad<center-before;++pad){m_disassembly->insertRow(pad);for(int col=0;col<5;++col)m_disassembly->setItem(pad,col,cell(""));}
     for(int i=0;i<before;++i)addTraceRow(center-before+i,trace[trace.size()-before+i].toMap(),false);
     m_disassemblyBase=quint16(pc);m_disassemblyBaseValid=true;quint16 address=m_disassemblyBase;
     for(int row=center;row<64;++row){const int index=int(quint16(address-programBase));if(index<0||index>=program.size())break;const int len=instructionLength(quint8(program[index]));if(!addDecodedRow(row,address,row==center))break;address=quint16(address+len);}
     if(m_disassembly->item(center,0))m_disassembly->scrollToItem(m_disassembly->item(center,0),QAbstractItemView::PositionAtCenter);
 }
 if(s.contains("memory")){m_memoryBytes=s.value("memory").toByteArray();unsigned base=s.value("memoryBase").toUInt();const int rows=(m_memoryBytes.size()+15)/16;m_memory->setUpdatesEnabled(false);m_memory->setRowCount(rows);for(int r=0;r<rows;++r){m_memory->setItem(r,0,cell(hx(base+r*16,4),QColor("#4FC3F7")));QString ascii;for(int c=0;c<16;++c){const int index=r*16+c;if(index>=m_memoryBytes.size()){m_memory->setItem(r,c+1,cell(""));ascii+=' ';continue;}quint8 v=quint8(m_memoryBytes[index]);m_memory->setItem(r,c+1,cell(hx(v,2)));ascii+=v>=32&&v<127?QChar(v):QChar('.');}m_memory->setItem(r,17,cell(ascii,QColor("#BFBF00")));}m_memory->setUpdatesEnabled(true);}
}
void AtariDebuggerWindow::addBreakpoint(){QString t=m_breakpointEdit->text().trimmed();if(t.startsWith('$'))t.remove(0,1);bool ok=false;uint v=t.toUInt(&ok,16);if(!ok||v>0xFFFF)return;QString a=hx(v,4);if(m_breakpointList->findItems(a,Qt::MatchExactly).isEmpty())m_breakpointList->addItem(a);m_breakpointEdit->clear();sendBreakpoints();}
void AtariDebuggerWindow::removeBreakpoint(){delete m_breakpointList->takeItem(m_breakpointList->currentRow());sendBreakpoints();}
void AtariDebuggerWindow::sendBreakpoints(){if(!m_controller)return;QVariantList l;for(int i=0;i<m_breakpointList->count();++i)l<<m_breakpointList->item(i)->text().toUInt(nullptr,16);QMetaObject::invokeMethod(m_controller,"setAtariDebuggerBreakpoints",Qt::QueuedConnection,Q_ARG(QVariantList,l));}
void AtariDebuggerWindow::goToMemoryAddress(){bool ok=false;uint address=m_memoryAddressEdit->text().toUInt(&ok,16);if(!ok)return;address=qMin(address,0x1FFFu)&0x1FF0u;m_memoryAddressEdit->setText(hx(address,4));if(auto* item=m_memory->item(int(address/16),0)){m_memory->setCurrentItem(item);m_memory->scrollToItem(item,QAbstractItemView::PositionAtTop);}}
void AtariDebuggerWindow::previousMemoryLine(){bool ok=false;uint address=m_memoryAddressEdit->text().toUInt(&ok,16);if(!ok)address=0;address=address>=16?address-16:0;m_memoryAddressEdit->setText(hx(address,4));goToMemoryAddress();}
void AtariDebuggerWindow::nextMemoryLine(){bool ok=false;uint address=m_memoryAddressEdit->text().toUInt(&ok,16);if(!ok)address=0;address=qMin((address&0x1FF0u)+16u,0x1FF0u);m_memoryAddressEdit->setText(hx(address,4));goToMemoryAddress();}
void AtariDebuggerWindow::homeMemory(){m_memoryAddressEdit->setText("0000");goToMemoryAddress();}
void AtariDebuggerWindow::copyMemoryRange(){
 QDialog dialog(this);dialog.setWindowTitle("Copy memory range to clipboard");dialog.setModal(true);dialog.setFixedSize(320,160);auto* layout=new QVBoxLayout(&dialog);auto* grid=new QGridLayout;auto* startEdit=new QLineEdit(m_memoryAddressEdit->text());auto* endEdit=new QLineEdit;bool ok=false;uint start=startEdit->text().toUInt(&ok,16);if(!ok)start=0;endEdit->setText(hx(qMin(start+0x1FFu,0x1FFFu),4));auto* validator=new QRegularExpressionValidator(QRegularExpression("[0-1]?[0-9a-fA-F]{1,3}"),&dialog);startEdit->setValidator(validator);endEdit->setValidator(validator);startEdit->setMaxLength(4);endEdit->setMaxLength(4);startEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));endEdit->setFont(startEdit->font());grid->addWidget(new QLabel("Start (hex):"),0,0);grid->addWidget(startEdit,0,1);grid->addWidget(new QLabel("End (hex):"),1,0);grid->addWidget(endEdit,1,1);layout->addLayout(grid);auto* buttons=new QHBoxLayout;buttons->addStretch();auto* accept=new QPushButton("OK");auto* cancel=new QPushButton("Cancel");buttons->addWidget(accept);buttons->addWidget(cancel);layout->addLayout(buttons);connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);connect(accept,&QPushButton::clicked,&dialog,[&]{bool firstOk=false,lastOk=false;uint first=startEdit->text().toUInt(&firstOk,16),last=endEdit->text().toUInt(&lastOk,16);if(!firstOk||!lastOk||first>0x1FFF||last>0x1FFF||m_memoryBytes.size()<0x2000)return;if(last<first)std::swap(first,last);QString output;for(uint address=first;address<=last;address+=16){output+=hx(address,4)+": ";for(uint column=0;column<16&&address+column<=last;++column)output+=hx(quint8(m_memoryBytes[int(address+column)]),2)+" ";output+='\n';}QGuiApplication::clipboard()->setText(output);dialog.accept();});dialog.exec();
}
void AtariDebuggerWindow::closeEvent(QCloseEvent* e){m_instructionRun=false;if(m_controller){QMetaObject::invokeMethod(m_controller,"setAtariDebuggerActive",Qt::QueuedConnection,Q_ARG(bool,false));QMetaObject::invokeMethod(m_controller,"runAtariDebugger",Qt::QueuedConnection);}QMainWindow::closeEvent(e);}
