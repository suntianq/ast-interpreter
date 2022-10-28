#include <stdio.h>
#include <iostream>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace std;
class StackFrame{
	/// StackFrame maps Variable Declaration to Value
	/// Which are either integer or addresses (also represented using an Integer value)
	std::map<Decl *, int64_t> mVars; 
	std::map<Stmt *, int64_t> mExprs;
	int64_t retValue = 0;

public:
	StackFrame() : mVars(), mExprs(){
	}

	void setReturn(int64_t val){		
		retValue = val;
	}
	int64_t getReturn(){
		return retValue;
	}
	void bindDecl(Decl *decl, int64_t val){
		mVars[decl] = val;
	}
	int64_t getDeclVal(Decl *decl){
		assert(mVars.find(decl) != mVars.end());
		return mVars.find(decl)->second;
	}
	void bindStmt(Stmt *stmt, int64_t val){
		mExprs[stmt] = val;
	}
	int64_t getStmtVal(Stmt *stmt){
		assert(mExprs.find(stmt) != mExprs.end());
		return mExprs[stmt];
	}

};


class Environment{
	FunctionDecl *mFree; 
	FunctionDecl *mMalloc;
	FunctionDecl *mInput;
	FunctionDecl *mOutput;
	FunctionDecl *mEntry;

public:
	std::vector<StackFrame> mStack;

	Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL){
	}

	void init(TranslationUnitDecl *unit){
		mStack.push_back(StackFrame()); 
		for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i){
			if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i)){
				if (fdecl->getName().equals("FREE"))
					mFree = fdecl;
				else if (fdecl->getName().equals("MALLOC"))
					mMalloc = fdecl;
				else if (fdecl->getName().equals("GET"))
					mInput = fdecl;
				else if (fdecl->getName().equals("PRINT"))
					mOutput = fdecl;
				else if (fdecl->getName().equals("main"))
					mEntry = fdecl;
			}
			else if (VarDecl *vardecl = dyn_cast<VarDecl>(*i)){
					if (vardecl->hasInit())
						mStack.back().bindDecl(vardecl, get_exprval(vardecl->getInit()));
					else
						mStack.back().bindDecl(vardecl, 0);
			}
		}
	}

	FunctionDecl *getEntry(){
		return mEntry;
	}

	void binop(BinaryOperator *bop){
		Expr *left = bop->getLHS();
		Expr *right = bop->getRHS();
		int64_t rightval=get_exprval(right);
		if (bop->isAssignmentOp()){
			if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)){
				Decl *decl = declexpr->getFoundDecl();
				mStack.back().bindDecl(decl, rightval);
			}
			else if(isa<ArraySubscriptExpr>(left)){
				auto array = dyn_cast<ArraySubscriptExpr>(left);
				int64_t indexval = get_exprval(array->getIdx());
				DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(array->getLHS()->IgnoreImpCasts());
				auto vardecl = dyn_cast<VarDecl>(declexpr->getFoundDecl());
				auto arr = dyn_cast<ConstantArrayType>(vardecl->getType().getTypePtr());
				if (arr->getElementType().getTypePtr()->isIntegerType())
					*((int *)mStack.back().getDeclVal(vardecl) + indexval) = rightval;
				else if (arr->getElementType().getTypePtr()->isCharType())
					*((char *)mStack.back().getDeclVal(vardecl) + indexval) = (char)rightval;
				else
					*((int64_t **)mStack.back().getDeclVal(vardecl) + indexval) = (int64_t *)rightval;
			}
			else if (auto unaryExpr = dyn_cast<UnaryOperator>(left))
				*((int64_t*)get_exprval(unaryExpr->getSubExpr()))=rightval;
		}
		else{
			auto op = bop->getOpcode();
			int64_t leftval = get_exprval(left);
			switch (op){
			case BO_Add: // +
				if (left->getType().getTypePtr()->isPointerType())
					mStack.back().bindStmt(bop, leftval + sizeof(int64_t) * rightval);
				else
					mStack.back().bindStmt(bop, leftval + rightval);
				break;
			case BO_Sub: // -
				mStack.back().bindStmt(bop, leftval - rightval);
				break;
			case BO_Mul: // *
				mStack.back().bindStmt(bop, leftval * rightval);
				break;
			case BO_Div:
				if (rightval == 0)
					cout << "error! can't div 0 " << endl;
				exit(0);
				mStack.back().bindStmt(bop, int64_t(leftval / rightval));
				break;
			case BO_LT: // <
				mStack.back().bindStmt(bop, leftval < rightval);
				break;
			case BO_GT: // >
				mStack.back().bindStmt(bop, leftval > rightval);
				break;
			case BO_EQ: // ==
				mStack.back().bindStmt(bop, leftval == rightval);
				break;
			case BO_LE:
				mStack.back().bindStmt(bop,leftval<=rightval);
				break;
			case BO_GE:
				mStack.back().bindStmt(bop,leftval>=rightval);
				break;
			default:
				cout << "error! Can't handle this BinaryOp" << endl;
				exit(0);
				break;
			}		
		}
	}

	void decl(DeclStmt *declstmt){
		for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			 it != ie; ++it){
			Decl *decl = *it;
			if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)){
				QualType type = vardecl->getType();
				if (type->isIntegerType() || type->isPointerType()){
					if (vardecl->hasInit())
						mStack.back().bindDecl(vardecl, get_exprval(vardecl->getInit()));
					else
						mStack.back().bindDecl(vardecl, 0);
				}
				else if(type->isArrayType()) {
						auto array = dyn_cast<ConstantArrayType>(type.getTypePtr());
						int64_t len = array->getSize().getSExtValue();
						if (array->getElementType().getTypePtr()->isIntegerType()){
							int *arraystore = new int[len];
							for (int i = 0; i < len; i++)
								arraystore[i] = 0;
							mStack.back().bindDecl(vardecl, (int64_t)arraystore);
						}
						else if (array->getElementType().getTypePtr()->isCharType()){
							char *arraystore = new char[len];
							for (int i = 0; i < len; i++)
								arraystore[i] = 0;
							mStack.back().bindDecl(vardecl, (int64_t)arraystore);
						}
						else{
						    // int* c[2];
							int64_t **arraystore = new int64_t *[len];
							for (int i = 0; i < len; i++)
								arraystore[i] = 0;
							mStack.back().bindDecl(vardecl, (int64_t)arraystore);
						}
				}
			}
		}
	}

	void returnstmt(ReturnStmt *returnStmt){
		mStack.back().setReturn(get_exprval(returnStmt->getRetValue()));
	}
		
	

	void unaryop(UnaryOperator *uop){ // - +
		auto op = uop->getOpcode();
		switch (op){
		case UO_Minus:
			mStack.back().bindStmt(uop, -1 * get_exprval(uop->getSubExpr()));
			break;
		case UO_Plus:
			mStack.back().bindStmt(uop, get_exprval(uop->getSubExpr()));
			break;
		case UO_Not:
			mStack.back().bindStmt(uop,~get_exprval(uop->getSubExpr()));
			break;
		case UO_LNot:
			mStack.back().bindStmt(uop,!get_exprval(uop->getSubExpr()));
			break;
		case UO_Deref: // '*'
			mStack.back().bindStmt(uop, *(int64_t *)get_exprval(uop->getSubExpr()));
			break;
		default:
			cout << "error! can't process unaryOp " << endl;
			exit(0);
			break;
		}
	}

	void bind_paexprval(ParenExpr *parenexpr){
		mStack.back().bindStmt(parenexpr,get_exprval(parenexpr->getSubExpr()));
	}

	int64_t get_exprval(Expr *expr){
		expr = expr->IgnoreImpCasts();
		if (auto decl = dyn_cast<DeclRefExpr>(expr)){
			declref(decl);
			return (int64_t)mStack.back().getStmtVal(decl);
		}
		else if (auto intliteral = dyn_cast<IntegerLiteral>(expr)) 
			return intliteral->getValue().getSExtValue();
		else if (auto charliteral = dyn_cast<CharacterLiteral>(expr))
			return charliteral->getValue(); 
		else if (auto uop = dyn_cast<UnaryOperator>(expr)){ 
			unaryop(uop);
			return mStack.back().getStmtVal(uop);
		}
		else if (auto bop = dyn_cast<BinaryOperator>(expr)){ 
			binop(bop);
			return mStack.back().getStmtVal(bop);
		}
		else if (auto pexpr = dyn_cast<ParenExpr>(expr)) 
			return mStack.back().getStmtVal(expr);
		else if (auto array = dyn_cast<ArraySubscriptExpr>(expr))// a[1]
			return mStack.back().getStmtVal(expr);
		else if (auto callexpr = dyn_cast<CallExpr>(expr))
			return mStack.back().getStmtVal(callexpr);
		else if(auto sizeofexpr = dyn_cast<UnaryExprOrTypeTraitExpr>(expr))
			return mStack.back().getStmtVal(sizeofexpr);
		else if (auto castexpr = dyn_cast<CStyleCastExpr>(expr))
			return get_exprval(castexpr->getSubExpr());
		cout << "error! can't handle the expression" << endl;
		return 0;
	}
	void bind_array(ArraySubscriptExpr *arraysubscript){
		int64_t indexval = get_exprval(arraysubscript->getIdx());
		DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(arraysubscript->getLHS()->IgnoreImpCasts());
		VarDecl *vardecl = dyn_cast<VarDecl>(declexpr->getFoundDecl());
		auto arr = dyn_cast<ConstantArrayType>(vardecl->getType().getTypePtr());
		if (arr->getElementType().getTypePtr()->isIntegerType())
			mStack.back().bindStmt(arraysubscript,*((int *)mStack.back().getDeclVal(vardecl)+indexval));
		else if(arr->getElementType().getTypePtr()->isCharType())
			mStack.back().bindStmt(arraysubscript,*((char *)mStack.back().getDeclVal(vardecl)+indexval));
		else	
			mStack.back().bindStmt(arraysubscript,(int64_t)(*((int64_t **)mStack.back().getDeclVal(vardecl)+indexval)));
	}

	void bind_ueot(UnaryExprOrTypeTraitExpr *ueotexpr){
		UnaryExprOrTypeTrait kind = ueotexpr->getKind();
		switch (kind)
		{
		case UETT_SizeOf:
			mStack.back().bindStmt(ueotexpr, (int64_t)8);
			break;
		default:
			llvm::errs() << "Unhandled UEOT.";
			break;
		}
	}

	void declref(DeclRefExpr *declref){
		if (declref->getType()->isIntegerType() || declref->getType()->isPointerType() || declref->getType()->isArrayType())
			mStack.back().bindStmt(declref, mStack.back().getDeclVal(declref->getFoundDecl()));
}
	
void call(CallExpr *callexpr){
		int64_t val = 0;
		FunctionDecl *callee = callexpr->getDirectCallee();
		if (callee == mInput)
		{
			cout << "Please Input an Integer Value : " << endl;
			cin >> val;
			mStack.back().bindStmt(callexpr, val);
		}
		else if (callee == mOutput){ 
			Expr *decl = callexpr->getArg(0);
			cout << get_exprval(decl) << endl;
		}
		else if (callee == mMalloc){
			int64_t malloc_size = get_exprval(callexpr->getArg(0));
			int64_t *p = (int64_t *)std::malloc(malloc_size);
			mStack.back().bindStmt(callexpr, (int64_t)p);
		}
		else if (callee == mFree){
			int64_t *p = (int64_t *)get_exprval(callexpr->getArg(0));
			std::free(p);
		}
		else{
			vector<int64_t> args;
			for (auto i = callexpr->arg_begin(); i != callexpr->arg_end(); i++)
				args.push_back(get_exprval(*i));
			mStack.push_back(StackFrame());
			int j = 0;
			for (auto i = callee->param_begin(); i !=callee->param_end(); i++, j++)
				mStack.back().bindDecl(*i, args[j]);
		}
	}
};