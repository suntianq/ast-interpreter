#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class ReturnException : public std::exception {};

class InterpreterVisitor : public EvaluatedExprVisitor<InterpreterVisitor>{
public:
   explicit InterpreterVisitor(const ASTContext &context, Environment *env)
       : EvaluatedExprVisitor(context), mEnv(env) {}
   virtual ~InterpreterVisitor() {}

   virtual void VisitBinaryOperator(BinaryOperator *bop){
      VisitStmt(bop);
      mEnv->binop(bop);
   }
   virtual void VisitDeclRefExpr(DeclRefExpr *expr){
      VisitStmt(expr);
      mEnv->declref(expr);
   }
   virtual void VisitParenExpr(ParenExpr *parenexpr){
      VisitStmt(parenexpr);
      mEnv->bind_paexprval(parenexpr);
   }

   virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *expr) {
    mEnv->bind_ueot(expr);
  }


   virtual void VisitCallExpr(CallExpr *call){
      VisitStmt(call);
      mEnv->call(call);
      if ((!call->getDirectCallee()->getName().equals("GET")) &&
         (!call->getDirectCallee()->getName().equals("PRINT")) &&
         (!call->getDirectCallee()->getName().equals("MALLOC")) &&
         (!call->getDirectCallee()->getName().equals("FREE"))){
             try {
      VisitStmt(call->getDirectCallee()->getBody());
    } catch (ReturnException e) {
    }
            int64_t retvalue = mEnv->mStack.back().getReturn();
            mEnv->mStack.pop_back();
            mEnv->mStack.back().bindStmt(call, retvalue);
         }
   }

   virtual void VisitDeclStmt(DeclStmt *declstmt){
      VisitStmt(declstmt);
      mEnv->decl(declstmt);
   }

   virtual void VisitArraySubscriptExpr(ArraySubscriptExpr *arrayexpr) {
    VisitStmt(arrayexpr);
    mEnv->bind_array(arrayexpr);
  }

   virtual void VisitIfStmt(IfStmt *ifstmt){

      Expr *cond = ifstmt->getCond();
      Visit(cond);
      if (mEnv->get_exprval(cond))
         Visit(ifstmt->getThen()); 
      else{
         if (ifstmt->getElse())
            Visit(ifstmt->getElse());
      }
   }

   virtual void VisitWhileStmt(WhileStmt *whilestmt){
      while (mEnv->get_exprval(whilestmt->getCond())){
         Visit(whilestmt->getBody());
      }
   }

   virtual void VisitForStmt(ForStmt *forstmt){
      Stmt *forinit = forstmt->getInit();
      Expr *forcond = forstmt->getCond();
      Expr *forinc = forstmt->getInc();
      Stmt *forbody = forstmt->getBody();
      if (forinit)
         Visit(forinit);
       while(mEnv->get_exprval(forcond)){
         Visit(forbody);
         Visit(forinc);
      }
   }

   virtual void VisitReturnStmt(ReturnStmt *ret){
      VisitStmt(ret);
      mEnv->returnstmt(ret);
      throw ReturnException();
   }

private:
   Environment *mEnv;
};

class InterpreterConsumer : public ASTConsumer
{
public:
   explicit InterpreterConsumer(const ASTContext &context) : mEnv(),
         mVisitor(context, &mEnv){
   }
   virtual ~InterpreterConsumer() {}

   virtual void HandleTranslationUnit(clang::ASTContext &Context){
      TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
      mEnv.init(decl);

      FunctionDecl *entry = mEnv.getEntry();
      try {
      mVisitor.VisitStmt(entry->getBody());
    } catch (ReturnException e) {
    }
   }

private:
   Environment mEnv;
   InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction{
public:
   virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
       clang::CompilerInstance &Compiler, llvm::StringRef InFile){
      return std::unique_ptr<clang::ASTConsumer>(
          new InterpreterConsumer(Compiler.getASTContext()));
   }
};

int main(int argc, char **argv){
   if (argc > 1){
      clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
   }
}