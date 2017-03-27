#include "pdgBuilder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
/*!
 * The algorithm for building the relations:
 * We specialize Statement with dyn_cast
 * Then add general children.
 */
using namespace clang::ast_matchers;

namespace clang {
namespace slicer {
namespace {
AST_MATCHER(BinaryOperator, isAssignmentOp) {
  return Node.isAssignmentOp();
}
AST_MATCHER(UnaryOperator, isIncrementDecrementOp) {
  return Node.isIncrementDecrementOp();
}
}

void PDGBuilder::registerMatchers(MatchFinder *MatchFinder) {
  // first define matchers
  // assign
  auto decls = declStmt(hasDescendant(varDecl().bind("decl"))).bind("declStmt");
  auto declWithInit = declStmt(hasDescendant(
                                  varDecl(forEachDescendant(
                                    declRefExpr(to(varDecl().bind("declInit"))))).bind("declWithInit")
                              )).bind("declStmtWithInit");

  auto bop = binaryOperator(isAssignmentOp(),
                                hasLHS(ignoringImpCasts(declRefExpr(to(varDecl().bind("lval"))))),
                                hasRHS(forEachDescendant(
                                  expr(declRefExpr(to(varDecl().bind("rval"))))))).bind("binop");
  
  auto bopLit = binaryOperator(isAssignmentOp(),
	  hasLHS(ignoringImpCasts(declRefExpr(to(varDecl().bind("lval")))))).bind("binopLit");

  auto uop = unaryOperator(isIncrementDecrementOp(),
                           hasDescendant(declRefExpr(to(varDecl().bind("uval"))))).bind("unop");
  // branch
  //fixme it won't match for "if(0)"
  auto ifs = ifStmt(hasCondition(
    forEachDescendant(declRefExpr(to(varDecl().bind("ifCondVar")))))).bind("if");
  // loop
  auto whiles = whileStmt(hasCondition(
    forEachDescendant(declRefExpr(to(varDecl().bind("whileCondVar")))))).bind("while");
  // compound
  auto compounds = compoundStmt().bind("comp");
  // find slicing variable
  auto slicingVar = declRefExpr().bind("slicingVar");

  // then add them to MatchFinder
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(decls)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(declWithInit)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(bop)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(bopLit)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(uop)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(ifs)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(whiles)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(compounds)).bind("f"), this);
  MatchFinder->addMatcher(functionDecl(hasName(funcName), forEachDescendant(slicingVar)).bind("f"), this);
}
void PDGBuilder::run(const ast_matchers::MatchFinder::MatchResult &result) {
  // process the match results
  // ensure every entry added once
  // assign
  if (auto ds = result.Nodes.getNodeAs<DeclStmt>("declStmt")) {
    //todo multiple decl...
    auto d = result.Nodes.getNodeAs<VarDecl>("decl");
    if (!hasStmt(ds)) {
		stmt_map[ds] = new AssignStatement(ds, getLoc(result, ds), { d });
    }
    else {
      stmt_map[ds]->addDefine(d);
    }
  }
  if (auto ds = result.Nodes.getNodeAs<DeclStmt>("declStmtWithInit")) {
    //todo multiple decl...
    auto d = result.Nodes.getNodeAs<VarDecl>("declWithInit");
    if (!hasStmt(ds)) {
		stmt_map[ds] = new AssignStatement(ds, getLoc(result, ds), { d });
    }
    else {
      stmt_map[ds]->addDefine(d);
    }
    stmt_map[ds]->addUse(result.Nodes.getNodeAs<VarDecl>("declInit"));
  }
  if (auto bo = result.Nodes.getNodeAs<BinaryOperator>("binop")) {
	auto lhs = result.Nodes.getNodeAs<VarDecl>("lval");
    if (!hasStmt(bo)) {
		stmt_map[bo] = new AssignStatement(bo, getLoc(result, bo), { lhs });
    }
    else {
      stmt_map[bo]->addDefine(lhs);
    }
      stmt_map[bo]->addUse(result.Nodes.getNodeAs<VarDecl>("rval"));
  }
  if (auto boL = result.Nodes.getNodeAs<BinaryOperator>("binopLit")) {
	  auto lhs = result.Nodes.getNodeAs<VarDecl>("lval");
	  if (!hasStmt(boL)) {
		  stmt_map[boL] = new AssignStatement(boL, getLoc(result, boL), { lhs });
	  }
	  else {
		  stmt_map[boL]->addDefine(lhs);
	  }
  }
  if (auto uo = result.Nodes.getNodeAs<UnaryOperator>("unop")) {
    auto var = result.Nodes.getNodeAs<VarDecl>("uval");
    if (!hasStmt(uo)) {
		stmt_map[uo] = new AssignStatement(uo, getLoc(result, uo), { var });
    }
    else {
      stmt_map[uo]->addDefine(var);
    }
    stmt_map[uo]->addUse(var);
  }
  // branch
  if (auto is = result.Nodes.getNodeAs<IfStmt>("if")) {
    if (!hasStmt(is)) {
      stmt_map[is->getThen()] = Statement::create(is->getThen(),getLoc(result,is->getThen()));
	  if (is->getElse() == nullptr) {
		stmt_map[is] = new BranchStatement(is, getLoc(result,is) ,
                                               { {stmt_map[is->getThen()],Statement::Edge::True} });
	  }
	  else {
		  stmt_map[is->getElse()] = Statement::create(is->getElse(), getLoc(result, is->getElse()));
		  stmt_map[is] = new BranchStatement(is, getLoc(result, is),
												  { { stmt_map[is->getThen()],Statement::Edge::True },
												  { stmt_map[is->getElse()],Statement::Edge::True } });
	  }
    }
      stmt_map[is]->addUse(result.Nodes.getNodeAs<VarDecl>("ifCondVar"));

  }
  // loop
  if (auto ws = result.Nodes.getNodeAs<WhileStmt>("while")) {
    if (!hasStmt(ws)) {
      stmt_map[ws->getBody()] = Statement::create(ws->getBody(),getLoc(result,ws->getBody()));
      stmt_map[ws] = new LoopStatement(ws, getLoc(result,ws),{ {stmt_map[ws->getBody()],Statement::Edge::True} });
    }
      stmt_map[ws]->addUse(result.Nodes.getNodeAs<VarDecl>("whileCondVar"));
  }
  // compound
  if (auto cs = result.Nodes.getNodeAs<CompoundStmt>("comp")) {
    if (!hasStmt(cs)) {
      stmt_map[cs] = new CompoundStatement(cs,getLoc(result,cs));
    }
    if (stmt_map[cs]->getControlChildren().empty()) {
      for (auto c : cs->children()) {
        if (stmt_map.find(c) == stmt_map.end()) {
          stmt_map[c] = Statement::create(c,getLoc(result,c));
        }
        stmt_map[cs]->addControlChild({ stmt_map[c],Statement::Edge::True });
      }
    }
  }
  if (auto f = result.Nodes.getNodeAs<FunctionDecl>("f")) {
    //root
    root = f->getBody();
    if (!hasStmt(root)) {
      stmt_map[root] = new CompoundStatement(root,getLoc(result,root));
      for (auto& var : f->parameters()) {
        stmt_map[root]->addDefine(var);
      }
    }
  }
  if (auto sv = result.Nodes.getNodeAs<DeclRefExpr>("slicingVar")) {
    clang::FullSourceLoc loc = getLoc(result, sv);
    if (loc.getSpellingLineNumber() == lineNo &&
        loc.getSpellingColumnNumber() == colNo) {
      slicingVar = sv->getDecl();
      sm = result.SourceManager;
    }
  }
}

void PDGBuilder::onEndOfTranslationUnit() {
  // slice here
  if (slicingVar) { llvm::errs() << "slicing variable is: " + slicingVar->getNameAsString() + "\n\n"; }
  else { llvm::errs() << "You've given invalid location for slicing var, since I've found no variable there.\n"; }
  llvm::errs() << "With control edges, but no data dependence edges:\n";
  llvm::errs() << stmt_map[root]->dump();
  stmt_map[root]->setDataEdges();
  llvm::errs() << "With data dependence edges too:\n";
  llvm::errs() << stmt_map[root]->dump();
  llvm::errs() << stmt_map[root]->dumpDot(*sm);

}
} // namespace slicer
} // namespace clang