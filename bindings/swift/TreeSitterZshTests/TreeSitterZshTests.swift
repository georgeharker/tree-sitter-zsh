import XCTest
import SwiftTreeSitter
import TreeSitterZsh

final class TreeSitterZshTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_zsh())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Bash grammar")
    }
}
