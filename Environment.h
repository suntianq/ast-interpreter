//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include <iostream>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, int> mVars;
   std::map<Stmt*, int> mExprs;
   /// The current stmt
   Stmt * mPC;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDecl(Decl* decl, int val) { //给Decl赋值操作
      mVars[decl] = val;
   }    
   int getDeclVal(Decl * decl) {   //返回Decl的值
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   void bindStmt(Stmt * stmt, int val) {  //给Expr赋值操作
	   mExprs[stmt] = val;
   }
   int getStmtVal(Stmt * stmt) {  //获得Stmt的值的操作
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }
   void setPC(Stmt * stmt) {  //将当前的Stmt赋值给mPC
	   mPC = stmt;
   }
   Stmt * getPC() {   //获取当前的Stmt
	   return mPC;
   }
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class Environment {
   std::vector<StackFrame> mStack;

   FunctionDecl * mFree;				/// Declartions to the built-in functions
   FunctionDecl * mMalloc;
   FunctionDecl * mInput;
   FunctionDecl * mOutput;

   FunctionDecl * mEntry;   //main函数入口
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
   }


   /// Initialize the Environment
   void init(TranslationUnitDecl * unit) {
	   for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
		   }
	   }
	   mStack.push_back(StackFrame());
   }

   FunctionDecl * getEntry() {
	   return mEntry;
   }

   /// !TODO Support comparison operation
   void binop(BinaryOperator *bop) {
	   	Expr * left = bop->getLHS();
	   	Expr * right = bop->getRHS();
		int val = 0;
	   	if (bop->isAssignmentOp()) {
			int val = get_exprval(right);   //需要先bindstmt，后getStmtVal
		   	mStack.back().bindStmt(left, val);
		   	if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
			   	Decl * decl = declexpr->getFoundDecl();
			   	mStack.back().bindDecl(decl, val);
		   }
	   }
	   else{
		auto op = bop->getOpcode();
		// std::cout<<"op:"<<op<<std::endl; //debug
		int leftval=get_exprval(left);
		int rightval=get_exprval(right);
		switch (op){
			case BO_Add:  // +
				mStack.back().bindStmt(bop,leftval+rightval);
				break;
			case BO_Sub: // - 
				mStack.back().bindStmt(bop,leftval-rightval);
				break;
			case BO_Mul: // ×
				mStack.back().bindStmt(bop,leftval*rightval);
				break;
			case BO_Div: // ÷
				assert(rightval!=0);
				mStack.back().bindStmt(bop,leftval/rightval);
				break;
			case BO_EQ: // ==
				mStack.back().bindStmt(bop,leftval==rightval);
				break;
			case BO_LT: // < 
				mStack.back().bindStmt(bop,leftval<rightval);
				break;
			case BO_GT: // >
				mStack.back().bindStmt(bop,leftval>rightval);
				break;
			case BO_NE:
				mStack.back().bindStmt(bop,leftval!=rightval);
				break;
			case BO_LE:
				mStack.back().bindStmt(bop,leftval<=rightval);
				break;
			case BO_GE:
				mStack.back().bindStmt(bop,leftval>=rightval);
				break;
			default:
				std::cout<<"Only the following operations are supported:+ - × ÷ == < >"<<std::endl;
				exit(0);
				break;

		}
	   }
   }

   int get_exprval(Expr *expr){
		expr = expr->IgnoreImpCasts();
		if(auto intliteral = dyn_cast<IntegerLiteral>(expr)){
			return intliteral->getValue().getSExtValue();
		}
		else if(auto bop = dyn_cast<BinaryOperator>(expr)){
			binop(bop);
			return mStack.back().getStmtVal(bop);
		}
		else if(auto refdecl = dyn_cast<DeclRefExpr>(expr)){
			declref(refdecl);
			return mStack.back().getStmtVal(refdecl);
		}
   }

   void decl(DeclStmt * declstmt) {
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
				if(vardecl->hasInit()){
					mStack.back().bindDecl(vardecl,get_exprval(vardecl->getInit()));
				}
				else
					mStack.back().bindDecl(vardecl,0);
		   }
	   }
   }
   void declref(DeclRefExpr * declref) {
	   mStack.back().setPC(declref);
	   if (declref->getType()->isIntegerType()) {
		   Decl* decl = declref->getFoundDecl();  //getFoundDecl()：获取发生此引用的 NamedDecl
		   int val = mStack.back().getDeclVal(decl);
		   mStack.back().bindStmt(declref, val);
	   }
   }

   void cast(CastExpr * castexpr) {
	   mStack.back().setPC(castexpr);
	   if (castexpr->getType()->isIntegerType()) {
		   Expr * expr = castexpr->getSubExpr();
		   int val = mStack.back().getStmtVal(expr);
		   mStack.back().bindStmt(castexpr, val );
	   }
   }

   /// !TODO Support Function Call
   void call(CallExpr * callexpr) {
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%d", &val);
		  mStack.back().bindStmt(callexpr, val);
	   } else if (callee == mOutput) {
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val;
	   } else {
		   /// You could add your code here for Function call Return
	   }
   }
};


