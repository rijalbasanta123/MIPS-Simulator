#include "gen.hpp"
using std::vector;
using std::string;
// FileException class will be moved to another file
// TODO : Maybe not use std::vector?

Generator::Generator (const char *content, const char *src, bool debug ):\
file(content),srcPath(src),parseSuccess(true),genSuccess(true),debugMode(debug),totalIns(0)\
{
    prog.reserve( 100 );
    objs.reserve( 100 );
}

Generator::~Generator () {
}

GenError :: GenError ( GenError :: Type t,const size_t s,const std::string &insString, const char *arg) : type(t), line(s),ins(insString), label(arg){
}

void Generator :: genError ( GenError &error ){
    errorBuffer.clear();
    switch ( error.type ){
        case GenError::NO_LABEL:
            assert( error.label );
            errorBuffer.append( "Label %s not found!", error.label );
            break;
        case GenError::BRANCH_OFFSET_LARGE:
            errorBuffer.append( "Branch offset address is too large/small for a 16-bit field( min %d, max %d ).", INT16_MIN, INT16_MAX );
            break;
        case GenError::JUMP_ADDR_LARGE:
            errorBuffer.append( "Jump offset address is too large/small for a 26-bit field( min %d, max %d ).", INT16_MIN, INT16_MAX );
            break;
        case GenError::NO_INS:
            errorBuffer.append("Unidentified pseudo instruction %s.", error.label );
            break; 
        case GenError::SHAMT_FIELD:
            errorBuffer.append("Value for shamt field exceeds 5-bits ( max %d ).", FIVE_BIT_MAX );
            break;
        case GenError::IMM_FIELD: 
            errorBuffer.append("Value for immediate field exceeds 16-bits ( max %d ).", SIXTEEN_BIT_MAX);
            break;
    }
    errorList.push_back( ErrorInfo( ErrorInfo::ErrorLocation :: ERR_GENERATOR,\
        error.line,\
        error.ins,\
        errorBuffer.getBuff()));
}
/*
 * -------------------------------------------------
 * | BRANCH OFFSET AND JUMP ADDRESS CALCULATION |
 * -------------------------------------------------
 * We calculate the word offset here.
 * The word offset is a signed 16-bit integer
 * Positive offsets denote a forward jump
 * Negative offsets denote a backward jump
 * When calculating we calculate the offset relative to the next instruction
 * The word offset is the number of words we have to pass 
 * from the END of the current instruction to the instruction after branching Label
 * which is the reason why there is no extra -1 
 * 
 * Jump addresses are absolute 
 * The address is an unsigned sixteen bit number
 * Similar to branch they are word addresses i.e they always point to a begining of a valid instruction
 * i.e The address is later multiplied by 4 to get the actual address of the instruction 
 */

bool Generator:: resolveBranch(const ParseObj *p) {
    if ( p->props.Branch.label ){
        auto search = labelMap.find( string (p->props.Branch.label) );
        if ( search == labelMap.end() ){ // Label not found
//            displayError( p, "Label %s not found!", p->props.Jump.label );
            throw GenError( GenError::Type::NO_LABEL, p->line, p->insString, p->props.Branch.label );
            return false;
        } else {
            p->props.Branch.offset = ( search->second - p->insNumber );
        }
    }
    if ( !IS_SIGNED_16( p->props.Branch.offset ) ){
        throw GenError( GenError::Type::BRANCH_OFFSET_LARGE, p->line, p->insString);
//        displayError(p, "Branch offset address is too large/small for a 16-bit field( min %d, max %d ).", INT16_MIN, INT16_MAX );
        return false; 
    }
    return true;
}

bool Generator :: resolveJump(const ParseObj *p){
    if ( p->props.Jump.label ){
        auto search = labelMap.find( string ( p->props.Jump.label) );
        if ( search == labelMap.end() ){
            throw GenError( GenError::Type::NO_LABEL, p->line, p->insString, p->props.Jump.label );
            //displayError( p , "Label %s not found!", p->props.Jump.label );
            return false;
        } else {
            size_t tmp = search->second  + ( ORIGIN >> 2 ) ; 
            /* 
             * The word address is calculated as follows:
             * search->second is the instruction number of the instruction imediately followed by the loop.
             * The number is there even if there is no instruction after the loop ( in this case the program simply terminates )
             * Then we add the memory address of the first instruction of the program ( ORIGIN ).
             * We also divide by 4 ( rshift by 2 ) to get the correct word address of the instruction. 
             * The machine running it will multiply by 4 to get the correct address ( hopefully ).
             */
            p->props.Jump.addr = tmp;
        }
    }
    if ( !IS_UNSIGNED_26( p->props.Jump.addr ) ){
        throw GenError( GenError::Type::JUMP_ADDR_LARGE, p->line, p->insString);
//        displayError( p, "Jump address is too large!");
        return false;
    }
    return true;
}


void Generator:: displayError(const ParseObj *p,const char *fmt, ... ){
    parseSuccess = false;
    genSuccess = false;
    enum { BUFFER_SIZE = 1024 };
    va_list args;
    va_start( args, fmt );
    char buffer[ BUFFER_SIZE ];
    size_t len = snprintf(buffer,BUFFER_SIZE,"At line %zu:\n"\
                        "Instruction : %s \nError: ", p->line, p->insString.c_str() );
    try {
        if ( len > BUFFER_SIZE ){
            throw "Allocated buffer is smaller than the error message !";
        }
    } catch (const char *msg){
        std::cerr << msg << std::endl;
    }
    len = vsnprintf(buffer+len,BUFFER_SIZE-(len+1),fmt,args);
    try {
        if ( len > BUFFER_SIZE-(len+1) ){
            throw "Allocated buffer is smaller than the error message !";
        }
    } catch (const char *msg){
        std::cerr << msg << std::endl;
    }
    va_end(args);
    std::cerr << buffer << std::endl << std::endl;
}

Code Generator::encodeItype( const ParseObj *p ){
    Code x = 0;
    /* 
     * Register values, opcodes and func fields are all guranteed to be at most 5-bits long.
     * So we skip the error checking for these values
     * shamt is given by the user so we have to check
     */
    x =  ( UINT32_CAST(p->ins.opcode) ) << RS_LEN;
    x  = ( x | UINT32_CAST(p->props.Itype.rs) ) << RT_LEN;
    x = ( x | UINT32_CAST(p->props.Itype.rt) ) << IMM_LEN;
    x  |= ( UINT32_CAST(p->props.Itype.addr) & SIXTEEN_BIT_MAX );
    return x;
}

Code Generator::encodeRtype( const ParseObj *p ){
    Code x = 0;
    /* 
     * Register values are all guranteed to be at most 5-bits long.
     * So we skip the error checking for these values
     */
    x =  ( UINT32_CAST(p->ins.opcode) ) << RS_LEN ;
    x  = ( x | UINT32_CAST(p->props.Rtype.rs) ) << RT_LEN;
    x = ( x | UINT32_CAST(p->props.Rtype.rt) ) << RD_LEN;
    x = ( x | UINT32_CAST(p->props.Rtype.rd) ) << SHAMT_LEN;
    x = ( x | ( UINT32_CAST(p->props.Rtype.shamt) & FIVE_BIT_MAX ) ) << FUNC_LEN;
    x |= p->ins.func;
    return x;
}

Code Generator::encodeBranch( const ParseObj *p ){
    Code x = 0;
    /* 
     * Register values are all guranteed to be at most 5-bits long.
     * So we skip the error checking for these values
     */
    x =  ( UINT32_CAST(p->ins.opcode) ) << RS_LEN ;
    x  = ( x | UINT32_CAST(p->props.Branch.rs) ) << RT_LEN;
    x = ( x | UINT32_CAST(p->props.Branch.rt) ) << IMM_LEN;
    x |= ( UINT32_CAST(p->props.Branch.offset) & SIXTEEN_BIT_MAX ) ;
    return x;
}

Code Generator::encodeJump( const ParseObj *p ){
    Code x = 0;
    /* 
     * Register values are all guranteed to be at most 5-bits long.
     * So we skip the error checking for these values
     */
    x =  ( UINT32_CAST(p->ins.opcode) ) << ADDRESS_LEN;

    /* 
     * Jump address are unsigned 26 bit numbers
     * You can't jump to a negative address
     */
    x |= ( UINT32_CAST(p->props.Jump.addr) & TWENTY_SIX_BIT_MAX ) ;
    return x;

}

Code Generator :: encodeJr ( const ParseObj *p ){
    Code x = 0;
    x = ( UINT32_CAST(p->props.Jr.rs) ) << ( RT_LEN + RD_LEN + SHAMT_LEN + FUNC_LEN);
    x |= ( UINT32_CAST(p->ins.func) & 0x3f );
    return x;
}

Code Generator :: encodePtype ( const ParseObj *p ){
    if ( p->ins.opcode == 0 ){
        switch( p->ins.func ){
            case 8:
                return encodeJr(p);
                break;
            default:
                break;
        }
    }
    throw GenError( GenError::Type::NO_INS, p->line, p->insString, p->ins.str.c_str() );
//    displayError(p,"Unidentified pseudo instruction %s.", p->ins.str.c_str());
    return 0; 
}
#if 0
Code Generator::encodeObj( const ParseObj *p){
    if ( p->ins.type() == Instruction::ITYPE ){
        return encodeItype(p);
    } else if ( p->ins.type() == Instruction::RTYPE ){
        return encodeRtype(p);
    } else if ( p->ins.type() == Instruction::BRTYPE){
        return encodeBranch(p);
    } else if ( p->ins.type() == Instruction :: JTYPE){
        return encodeJump(p);
    } else if ( p->ins.insClass == Instruction:: PTYPE ){
        return encodePtype(p);
    }
    std::cerr << "Invalid Object!" << std::endl;
    return 0;
}
#endif

bool Generator :: encode () {
    for ( size_t i = 0; i < objs.size(); i++ ){
        Code code;
        ParseObj *p = &objs[i];
        try {
            if ( p->ins.insClass == Instruction::RTYPE ){
                if ( p->ins.isShiftLogical() && !IS_UNSIGNED_5(p->props.Rtype.shamt) ){
                        throw GenError( GenError::Type::SHAMT_FIELD, p->line, p->insString );
                }
                code = encodeRtype( p );
            } else if ( p->ins.insClass == Instruction :: ITYPE ){
                if ( !IS_SIGNED_16(p->props.Itype.addr) ){
                        throw GenError( GenError::Type::IMM_FIELD, p->line, p->insString );
//                    displayError(p, "Value for Immediate field exceeds 16-bits ( min %d, max %d ).", INT16_MIN, INT16_MAX );
                }
                code = encodeItype(p);
            } else if ( p->ins.insClass == Instruction :: BRTYPE){
                resolveBranch(p);
                code = encodeBranch(p);
            } else if ( p->ins.insClass == Instruction :: JTYPE ){
                resolveJump(p);
                code = encodeJump(p);
            } else if ( p->ins.insClass == Instruction:: PTYPE ){
                code  = encodePtype(p);
            } else {
                throw GenError( GenError::Type::NO_INS, p->line, p->insString, p->ins.str.c_str() );
//                displayError(p,"Invalid instruction %s.",p->ins.str.c_str());
            }
            prog.push_back(code);
        }catch ( GenError &g ){
             genError( g );
        }
    }
    return genSuccess;
}

bool Generator :: parseFile() {
    try {
        if ( !file ){ throw "Generator has no file initialized!";}
    } catch ( const char *msg) {
        std::cerr << msg << std::endl;
    }
    Parser p(file);
    ParseObj obj = p.parse();
    while ( !p.isEnd() ){
        objs.push_back( obj );
        obj = p.parse();
    }
    totalIns = p.getInsCount();
    return p.isSuccess();
}

#define PROG_VERSION 1

ProgHeader::ProgHeader(size_t size, size_t offset):progSize(size),origin(offset){}
void MainHeader::setHeader(const char *s){
    memset(isa,0,16);
    snprintf(isa,16,"%s",s);
    version = PROG_VERSION;
    textOffset = sizeof(MainHeader) + sizeof(ProgHeader);
    phOffset = sizeof(MainHeader);
    dbgOffset = 0;
    secOffset = 0;
}

void Generator :: generateFile(const char *path){
    string s( (path)?path:"test.bin" );
    std::ofstream outFile( s, std::ofstream::out | std::ofstream::binary );
    if ( !outFile.is_open() || !outFile.good() ){
        const char *msg = strerror( errno );
        throw FileException( formatErr("Error! Cannot generate output file \'%s\':%s",s.c_str(),msg) );
        return;
    }
    size_t bytes = prog.size() * sizeof(Code); // size of the file that will be generated
    Code *data = prog.data();
    genMainHeader(outFile);
    genProgHeader(outFile, bytes);

    outFile.write(reinterpret_cast<char *>( data ), bytes );
    if ( debugMode ){
        genDebugSection(outFile);
    }
    if ( outFile.bad() ){
        const char *msg = strerror( errno );
        throw FileException( formatErr("Error! Cannot write to file \'%s\'. %s",s,msg) );
    }
    outFile.close();
}

void Generator :: genDebugSection(std::ofstream &outFile){
    vector <LineMapEntry> lineMap;
    lineMap.reserve(100); // reserve space for 100 entries
    genLineInfo(lineMap);

    DebugHeader dbg;
    dbg.lineMapSize = sizeof(LineMapEntry);
    dbg.lineMapCount = lineMap.size();
    if ( !realpath(srcPath,dbg.srcPath)){
        // error in determining the absolute path of the source file
        perror("Generator Error : Unable to find the correct source file path. ");
        genSuccess = false;
    }

    outFile.write(reinterpret_cast<char *>(&dbg),sizeof(DebugHeader));
    outFile.write(reinterpret_cast<char *>(&lineMap[0]), sizeof(LineMapEntry) * dbg.lineMapCount );
     
}

void Generator::genMainHeader(std::ofstream &outFile){
    MainHeader mheader;
    mheader.setHeader("MIPS-32");
    if ( debugMode ){
        mheader.dbgOffset = sizeof(MainHeader) + sizeof(ProgHeader) + prog.size() * sizeof(Code);
    }
    outFile.write(reinterpret_cast<char *>(&mheader), sizeof(MainHeader) );
}

void Generator :: genProgHeader(std::ofstream &outFile, size_t progSize ){
    ProgHeader pheader( progSize, 0);
    outFile.write(reinterpret_cast<char *>(&pheader), sizeof(ProgHeader) );
}


void Generator :: displayObjs(){
    for ( size_t i = 0; i < objs.size() ; i++ ){
        std::cout<<"Instruction Code: "<< std::endl << "0x"<< std::hex << prog[i] << std::endl;
        objs[i].display();
    }
}

size_t Generator :: genLineInfo(vector <LineMapEntry> &lineMap){
    for ( size_t i = 0; i < objs.size(); i++ ){
        ParseObj p = objs[i];
        lineMap.push_back( (LineMapEntry){p.line,prog[i],i+1} );
    }
    return lineMap.size();
}


void Generator :: dumpObjs(){
    for ( size_t i = 0; i < objs.size(); i++ ){
        dumpBuff.append("Instruction Code:\n 0x%llx\n",prog[i]);
        objs[i].dumpToBuff(dumpBuff);
    }
}

void Generator :: dumpToFile( const char *path ){
    string s( (path)?path:"out.dump" );
    std::ofstream outFile( s, std::ofstream::out | std::ofstream::binary );
    if ( !outFile.is_open() || !outFile.good() ){
        const char *msg = strerror( errno );
        throw FileException( formatErr("Error! Cannot generate output file \'%s\':%s",s.c_str(),msg) );
        return;
    }
    size_t bytes = dumpBuff.len;
    char *data  = dumpBuff.buff;
    outFile.write(reinterpret_cast<char *>( data ), bytes );
    if ( outFile.bad() ){
        const char *msg = strerror( errno );
        throw FileException( formatErr("Error! Cannot write to file \'%s\'. %s",s,msg) );
    }
    outFile.close();
}

#if 0
void Generator :: test (){
    assert( IS_SIGNED_16( -8 ) );
    assert( IS_SIGNED_16( 123 ) );
    assert( !IS_SIGNED_16(-56789) );
    assert( !IS_SIGNED_16(56789) );
    string s ("Loop:sll $t1,$s3,2\n"\
             "      addr $t1,$t1,$s6\n"\
             "      lw $t0,0($t1)\n"\
             "      bne $t0,$s5,Exit\n"\
             "      addi $s3,$s3,1\n"\
             "      jmp Loop\n"\
             "Exit: beq $t0,$s5,Loop\n"\
             "      beq $t1,$t2,Exit\n"\
                    "jmp Exit2\n"\
                    "jal Exit\n"\
                    "jr $s2\n"\
             "Exit2: ");
    Generator g( s.c_str(), NULL, false );
    bool x = g.parseFile( );
    g.encode();
    for ( size_t i = 0; i < g.objs.size() ; i++ ){
        std::cout<<"Instruction Code: "<< "0x"<< std::hex << g.prog[i] << std::endl;
        g.objs[i].display();
    }
    (void)x;
}

#endif
