// Contains code for the command line interface of the debugger
// Run it as ./debug <program_name>
#include <debug.hpp>

AppendBuffer printBuffer;
const std::string registerNames[] = {
    "$zero",
    "$at",
    "$v0", "$v1",
    "$a0", "$a1", "$a2", "$a3",
    "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
    "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
    "$t8", "$t9",
    "$k0", "$k1",
    "$gp", "$sp", "$fp", "$ra",
};
void displayRegisters( const RegisterInfo *regs ){
     const Word *w = reinterpret_cast< const Word * >( regs );
     size_t len =  sizeof( RegisterInfo )/ sizeof(Word);
     for ( size_t i = 0; i < len ; i++ ){
          printf("%-6s = 0x%08x\n",registerNames[i].c_str(), w[i] );
     }
}
bool processCommand( Debugger &debug,const char *line ){
     std::string l( line );
     std::istringstream stream( l ); 
     std::string word;
     stream >> word;
     if ( word == "registers" ){
          RegisterInfo regs = debug.getRegisters();
          displayRegisters( &regs );
     } else if ( word == "step" ){
          debug.singleStep();
          if ( !debug.isHalted() ){
               debug.displayCurrentSource();
          } else {
               debug.printHaltedMessage();
          }
     } else if ( word == "break" ){
          int line;
          stream >> line;
          // Set break point at line x;
          if ( stream ){
               debug.setBreakPoint( line );
          } else {
               std::cout << "Invalid error" << std::endl;
          }
     } else if ( word == "continue" ){
          debug.continueExecution();
          debug.displayCurrentSource();
     } else if ( word == "exit" ){
          return true;
     } else if ( word == "mem" ){
          size_t addr = 0, count = 0;
          stream >> addr;
          stream >> count;
          if ( stream ){
               char *x = debug.getMem( printBuffer, addr, count );
               std::cout << x ;
          } else {
               std::cout << "Error!" << std::endl << std::endl;
          }
          printBuffer.clearBuff();
     }
     return false;
}
void runDebugger (Debugger &debug){
     const char *line;
     bool isExit = false;
     debug.displaySource( 1, 5 );
     while ( !isExit && ( line = linenoise(">>") ) ){
          isExit = processCommand( debug,line );
          linenoiseHistoryAdd( line );
          linenoiseFree( (void *)line ); 
     }
}

int main( int argc, char *argv[] ){
     if ( argc < 2 ){
          std::cout << "Input a file!" << std::endl;
          return -1; 
     }
     size_t size;
     char *buff; 
     Debugger debug;
     try{
          buff = Debug::loadFile( argv[1], &size );
          debug.loadProgram(buff,size);
     } catch ( OpenException &e ){
          std::cerr << "Error when opening the file" << std::endl;
          e.display();
     } catch ( InvalidFileError &f ){
          f.display();
     } catch ( MachineException &m ){
          m.display();
          return -1;
     }
     runDebugger( debug );
     delete []buff;
     return 0;
}
